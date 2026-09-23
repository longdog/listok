#include "opencv_leaf_detector.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace leaf::detail {
namespace {

enum class RejectReason { TooSmall, Outside, Other };

struct Candidate {
  std::vector<cv::Point> points;
  double area = 0;
  double solidity = 0;
  cv::Rect box;
  double confidence = 0;
  int rowMajorIndex = 0;
  int minY = 0;
  int minX = 0;
};

Outcome<LeafContour> contourError(ErrorCode code, const char* message) noexcept {
  return Outcome<LeafContour>::failure({code, Stage::LeafContour, message});
}

bool finiteConfidence(double value) noexcept {
  return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

}  // namespace

Outcome<LeafContour> detectLeafContour(const PreprocessResult& image,
                                       const DetectionConfig& config) noexcept {
  try {
    if (image.binary.empty() || image.binary.type() != CV_8UC1 || image.binary.cols < 3 ||
        image.binary.rows < 3) {
      return contourError(ErrorCode::LeafNotFound, "empty mask");
    }
    const double frameArea = static_cast<double>(image.binary.cols) * image.binary.rows;
    if (!(frameArea > 0.0) || !std::isfinite(config.maxLeafAreaRatio) || config.maxLeafAreaRatio <= 0.0) {
      return contourError(ErrorCode::LeafNotFound, "frame");
    }

    cv::Mat binary = image.binary.clone();
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);

    std::vector<Candidate> accepted;
    bool sawTooSmall = false;
    bool sawOutside = false;
    int opencvIndex = 0;
    for (const auto& contour : contours) {
      const int index = opencvIndex++;
      if (contour.size() < 3) {
        continue;
      }
      const double area = std::abs(cv::contourArea(contour));
      const double areaRatio = area / frameArea;
      const cv::Rect box = cv::boundingRect(contour);
      int minY = std::numeric_limits<int>::max();
      int minX = std::numeric_limits<int>::max();
      bool outside = false;
      for (const cv::Point& point : contour) {
        minY = std::min(minY, point.y);
        minX = std::min(minX, point.x);
        if (point.x < config.frameMarginPx || point.y < config.frameMarginPx ||
            point.x >= image.binary.cols - config.frameMarginPx ||
            point.y >= image.binary.rows - config.frameMarginPx) {
          outside = true;
        }
      }
      if (areaRatio < config.minLeafAreaRatio) {
        sawTooSmall = true;
        continue;
      }
      if (areaRatio > config.maxLeafAreaRatio) {
        continue;
      }
      if (outside) {
        sawOutside = true;
        continue;
      }
      std::vector<cv::Point> hull;
      cv::convexHull(contour, hull);
      const double hullArea = std::abs(cv::contourArea(hull));
      const double solidity = hullArea > 0.0 ? area / hullArea : 0.0;
      if (!std::isfinite(solidity) || solidity < config.minContourSolidity) {
        continue;
      }
      // maxLeafAreaRatio is an acceptance ceiling, not a target size. Score size
      // inside the accepted band so a solid leaf below that ceiling stays above
      // minimumStageConfidence.
      const double span = config.maxLeafAreaRatio - config.minLeafAreaRatio;
      double band = 1.0;
      if (std::isfinite(span) && span > 0.0) {
        band = (areaRatio - config.minLeafAreaRatio) / span;
      }
      band = std::clamp(band, 0.0, 1.0);
      const double sizeScore = 0.5 + 0.5 * band;
      Candidate candidate;
      candidate.points = contour;
      candidate.area = area;
      candidate.solidity = solidity;
      candidate.box = box;
      candidate.confidence = std::clamp(solidity * sizeScore, 0.0, 1.0);
      candidate.rowMajorIndex = index;
      candidate.minY = minY;
      candidate.minX = minX;
      if (!finiteConfidence(candidate.confidence)) {
        return contourError(ErrorCode::InternalError, "confidence");
      }
      accepted.push_back(std::move(candidate));
    }

    if (accepted.empty()) {
      if (sawOutside) {
        return contourError(ErrorCode::LeafOutsideFrame, "frame margin");
      }
      if (sawTooSmall) {
        return contourError(ErrorCode::LeafTooSmall, "area");
      }
      return contourError(ErrorCode::LeafNotFound, "no contour");
    }

    std::sort(accepted.begin(), accepted.end(), [](const Candidate& a, const Candidate& b) {
      if (a.minY != b.minY) {
        return a.minY < b.minY;
      }
      if (a.minX != b.minX) {
        return a.minX < b.minX;
      }
      return a.rowMajorIndex < b.rowMajorIndex;
    });
    for (int index = 0; index < static_cast<int>(accepted.size()); ++index) {
      accepted[static_cast<std::size_t>(index)].rowMajorIndex = index;
    }
    if (static_cast<int>(accepted.size()) > config.maxContourCandidates) {
      accepted.resize(static_cast<std::size_t>(config.maxContourCandidates));
    }

    std::stable_sort(accepted.begin(), accepted.end(), [](const Candidate& a, const Candidate& b) {
      if (a.confidence != b.confidence) {
        return a.confidence > b.confidence;
      }
      if (a.area != b.area) {
        return a.area > b.area;
      }
      if (a.box.y != b.box.y) {
        return a.box.y < b.box.y;
      }
      if (a.box.x != b.box.x) {
        return a.box.x < b.box.x;
      }
      return a.rowMajorIndex < b.rowMajorIndex;
    });

    if (accepted.front().confidence < config.minimumStageConfidence) {
      return contourError(ErrorCode::LeafNotFound, "confidence");
    }
    if (accepted.size() >= 2 &&
        accepted[0].confidence - accepted[1].confidence < config.ambiguityScoreDelta) {
      return contourError(ErrorCode::AmbiguousLeafContour, "top two");
    }

    const Candidate& best = accepted.front();
    const ResizeTransform& transform = image.transform;
    LeafContour leaf;
    leaf.path.reserve(best.points.size() + 1);
    for (const cv::Point& point : best.points) {
      leaf.path.push_back(transform.toSource(Point{static_cast<double>(point.x),
                                                    static_cast<double>(point.y)}));
    }
    if (!leaf.path.empty() &&
        (leaf.path.front().x != leaf.path.back().x || leaf.path.front().y != leaf.path.back().y)) {
      leaf.path.push_back(leaf.path.front());
    }
    const double scale = transform.sourcePerWorkingX * transform.sourcePerWorkingY;
    leaf.area = best.area * scale;
    leaf.boundingBox = {transform.toSource(Point{static_cast<double>(best.box.x),
                                                 static_cast<double>(best.box.y)}).x,
                        transform.toSource(Point{static_cast<double>(best.box.x),
                                                 static_cast<double>(best.box.y)}).y,
                        best.box.width * transform.sourcePerWorkingX,
                        best.box.height * transform.sourcePerWorkingY};
    leaf.confidence = best.confidence;
    return Outcome<LeafContour>::success(std::move(leaf));
  } catch (const std::bad_alloc&) {
    return contourError(ErrorCode::InternalError, "allocation");
  } catch (...) {
    return contourError(ErrorCode::InternalError, "contour");
  }
}

}  // namespace leaf::detail
