#include "center_vein_detector.h"

#include "../../geometry/polyline.h"
#include "../../skeleton/graph.h"
#include "../../skeleton/thinning.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace leaf::detail {
namespace {

Outcome<CenterVein> veinError(ErrorCode code, const char* message) noexcept {
  return Outcome<CenterVein>::failure({code, Stage::CenterVein, message});
}

cv::Mat contourMask(const LeafContour& contour, cv::Size size) {
  cv::Mat mask(size, CV_8UC1, cv::Scalar(0));
  std::vector<cv::Point> polygon;
  polygon.reserve(contour.path.size());
  for (const Point& point : contour.path) {
    polygon.push_back(cv::Point(static_cast<int>(std::lround(point.x)),
                                static_cast<int>(std::lround(point.y))));
  }
  if (polygon.size() >= 3) {
    const std::vector<std::vector<cv::Point>> polygons{polygon};
    cv::fillPoly(mask, polygons, cv::Scalar(255));
  }
  return mask;
}

void bridgeCardinalGaps(cv::Mat& binary, int maxGap) {
  if (maxGap <= 0 || binary.empty()) {
    return;
  }
  cv::Mat original = binary.clone();
  for (int y = 0; y < original.rows; ++y) {
    int runStart = -1;
    bool seen = false;
    for (int x = 0; x < original.cols; ++x) {
      const bool on = original.at<std::uint8_t>(y, x) != 0;
      if (on) {
        if (seen && runStart >= 0 && x - runStart - 1 <= maxGap && x - runStart - 1 > 0) {
          for (int fill = runStart + 1; fill < x; ++fill) {
            binary.at<std::uint8_t>(y, fill) = 255;
          }
        }
        seen = true;
        runStart = x;
      }
    }
  }
  original = binary.clone();
  for (int x = 0; x < original.cols; ++x) {
    int runStart = -1;
    bool seen = false;
    for (int y = 0; y < original.rows; ++y) {
      const bool on = original.at<std::uint8_t>(y, x) != 0;
      if (on) {
        if (seen && runStart >= 0 && y - runStart - 1 <= maxGap && y - runStart - 1 > 0) {
          for (int fill = runStart + 1; fill < y; ++fill) {
            binary.at<std::uint8_t>(fill, x) = 255;
          }
        }
        seen = true;
        runStart = y;
      }
    }
  }
}

cv::Mat ridgeDomain(const cv::Mat& leaf) {
  // Polygon fill can leave a one-pixel rim of background inside the mask. A
  // large vein kernel turns that rim into a border skeleton, so the center
  // line never becomes its own base-to-apex path.
  cv::Mat interior;
  const cv::Mat element = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
  cv::erode(leaf, interior, element);
  if (cv::countNonZero(interior) == 0) {
    return leaf.clone();
  }
  return interior;
}

cv::Mat ridgeMask(const cv::Mat& gray, const cv::Mat& leaf, const DetectionConfig& config) {
  cv::Mat prepared = gray.clone();
  prepared.setTo(cv::mean(gray, leaf), leaf == 0);
  int kernel = config.veinThresholdBlockSize;
  if (kernel % 2 == 0) {
    ++kernel;
  }
  const int limit = std::max(3, (std::min(gray.cols, gray.rows) / 2) * 2 + 1);
  kernel = std::min(std::max(kernel, 3), limit);
  const cv::Mat element = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(kernel, kernel));
  cv::Mat blackhat;
  cv::Mat tophat;
  cv::morphologyEx(prepared, blackhat, cv::MORPH_BLACKHAT, element);
  cv::morphologyEx(prepared, tophat, cv::MORPH_TOPHAT, element);
  cv::Mat ridges;
  cv::max(blackhat, tophat, ridges);
  ridges.setTo(0, leaf == 0);
  int block = kernel;
  if (block % 2 == 0) {
    ++block;
  }
  block = std::min(block, std::max(3, (std::min(gray.cols, gray.rows) | 1)));
  if (block % 2 == 0) {
    --block;
  }
  block = std::max(block, 3);
  cv::Mat binary;
  cv::adaptiveThreshold(ridges, binary, 255, cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY, block,
                        -std::abs(config.veinThresholdC));
  binary.setTo(0, leaf == 0);
  bridgeCardinalGaps(binary, config.maxCenterVeinGapPx);
  return binary;
}

double chordWidth(const cv::Mat& leaf, Point endpoint, Point direction) {
  const double length = std::hypot(direction.x, direction.y);
  if (length <= 1e-9) {
    return 0;
  }
  const Point normal{-direction.y / length, direction.x / length};
  auto inside = [&leaf](int x, int y) {
    return x >= 0 && y >= 0 && x < leaf.cols && y < leaf.rows && leaf.at<std::uint8_t>(y, x) != 0;
  };
  const int x0 = static_cast<int>(std::lround(endpoint.x));
  const int y0 = static_cast<int>(std::lround(endpoint.y));
  int count = inside(x0, y0) ? 1 : 0;
  for (int sign : {-1, 1}) {
    for (int step = 1; step < std::max(leaf.cols, leaf.rows); ++step) {
      const int x = static_cast<int>(std::lround(endpoint.x + sign * step * normal.x));
      const int y = static_cast<int>(std::lround(endpoint.y + sign * step * normal.y));
      if (!inside(x, y)) {
        break;
      }
      ++count;
    }
  }
  return static_cast<double>(count);
}

struct PathCandidate {
  std::vector<Point> points;
  double length = 0;
  double score = 0;
  double minY = 0;
  double minX = 0;
  int id = 0;
};

std::vector<int> shortestPath(const SkeletonGraph& graph, int from, int to) {
  const int count = static_cast<int>(graph.nodes.size());
  std::vector<int> parent(static_cast<std::size_t>(count), -1);
  std::vector<char> seen(static_cast<std::size_t>(count), 0);
  std::queue<int> queue;
  queue.push(from);
  seen[static_cast<std::size_t>(from)] = 1;
  while (!queue.empty()) {
    const int current = queue.front();
    queue.pop();
    if (current == to) {
      break;
    }
    for (const std::uint32_t edge : graph.nodes[static_cast<std::size_t>(current)].edges) {
      const int next = static_cast<int>(edge);
      if (next < 0 || next >= count || seen[static_cast<std::size_t>(next)]) {
        continue;
      }
      seen[static_cast<std::size_t>(next)] = 1;
      parent[static_cast<std::size_t>(next)] = current;
      queue.push(next);
    }
  }
  if (!seen[static_cast<std::size_t>(to)]) {
    return {};
  }
  std::vector<int> reversed;
  for (int cursor = to; cursor >= 0; cursor = parent[static_cast<std::size_t>(cursor)]) {
    reversed.push_back(cursor);
    if (cursor == from) {
      break;
    }
  }
  std::reverse(reversed.begin(), reversed.end());
  return reversed;
}

double longitudinalExtent(const cv::Mat& leaf, Point start, Point end) {
  const Point delta{end.x - start.x, end.y - start.y};
  const double length = std::hypot(delta.x, delta.y);
  if (length <= 1e-9) {
    return 0;
  }
  const Point axis{delta.x / length, delta.y / length};
  double minProjection = std::numeric_limits<double>::infinity();
  double maxProjection = -minProjection;
  for (int y = 0; y < leaf.rows; ++y) {
    for (int x = 0; x < leaf.cols; ++x) {
      if (leaf.at<std::uint8_t>(y, x) == 0) {
        continue;
      }
      const double projection = x * axis.x + y * axis.y;
      minProjection = std::min(minProjection, projection);
      maxProjection = std::max(maxProjection, projection);
    }
  }
  if (!std::isfinite(minProjection) || !std::isfinite(maxProjection)) {
    return 0;
  }
  return std::max(0.0, maxProjection - minProjection);
}

// A leaf/background step becomes a ridge that follows the contour and closes
// vein tips into loops. Remove that tangential band; radial vein tips stay.
void suppressContourParallelRing(cv::Mat& skeleton, const Path& contour, const cv::Mat& leaf) {
  if (skeleton.empty() || contour.size() < 2 || leaf.size() != skeleton.size()) {
    return;
  }
  cv::Mat distance;
  cv::distanceTransform(leaf, distance, cv::DIST_L2, 3);
  constexpr float kBandPx = 14.f;
  constexpr double kParallel = 0.7;
  const cv::Mat original = skeleton.clone();
  auto tangent = [&contour](double x, double y, double& tx, double& ty) {
    double best = std::numeric_limits<double>::infinity();
    tx = 1;
    ty = 0;
    for (std::size_t i = 1; i < contour.size(); ++i) {
      const double x0 = contour[i - 1].x;
      const double y0 = contour[i - 1].y;
      const double dx = contour[i].x - x0;
      const double dy = contour[i].y - y0;
      const double len2 = dx * dx + dy * dy;
      double t = 0;
      if (len2 > 0.0) {
        t = ((x - x0) * dx + (y - y0) * dy) / len2;
        t = std::clamp(t, 0.0, 1.0);
      }
      const double qx = x0 + t * dx;
      const double qy = y0 + t * dy;
      const double gap = std::hypot(x - qx, y - qy);
      if (gap < best) {
        best = gap;
        const double length = std::hypot(dx, dy);
        if (length > 0.0) {
          tx = dx / length;
          ty = dy / length;
        }
      }
    }
  };
  for (int y = 1; y < original.rows - 1; ++y) {
    for (int x = 1; x < original.cols - 1; ++x) {
      if (original.at<std::uint8_t>(y, x) == 0 || distance.at<float>(y, x) >= kBandPx) {
        continue;
      }
      int neighbors[2][2];
      int count = 0;
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          if ((dx == 0 && dy == 0) || original.at<std::uint8_t>(y + dy, x + dx) == 0) {
            continue;
          }
          if (count < 2) {
            neighbors[count][0] = dx;
            neighbors[count][1] = dy;
          }
          ++count;
        }
      }
      if (count != 2) {
        continue;
      }
      const double sx = static_cast<double>(neighbors[1][0] - neighbors[0][0]);
      const double sy = static_cast<double>(neighbors[1][1] - neighbors[0][1]);
      const double length = std::hypot(sx, sy);
      if (length <= 1e-9) {
        continue;
      }
      double tx = 1;
      double ty = 0;
      tangent(static_cast<double>(x), static_cast<double>(y), tx, ty);
      if (std::abs((sx / length) * tx + (sy / length) * ty) > kParallel) {
        skeleton.at<std::uint8_t>(y, x) = 0;
      }
    }
  }
  cv::Mat reached(skeleton.size(), CV_8UC1, cv::Scalar(0));
  std::queue<cv::Point> queue;
  for (int y = 0; y < skeleton.rows; ++y) {
    for (int x = 0; x < skeleton.cols; ++x) {
      if (skeleton.at<std::uint8_t>(y, x) != 0 && distance.at<float>(y, x) >= kBandPx) {
        reached.at<std::uint8_t>(y, x) = 255;
        queue.push(cv::Point(x, y));
      }
    }
  }
  // A thin shape has no interior skeleton. Leave it intact.
  if (queue.empty()) {
    skeleton = original.clone();
    return;
  }
  while (!queue.empty()) {
    const cv::Point current = queue.front();
    queue.pop();
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        const int nx = current.x + dx;
        const int ny = current.y + dy;
        if (nx < 0 || ny < 0 || nx >= skeleton.cols || ny >= skeleton.rows) {
          continue;
        }
        if (skeleton.at<std::uint8_t>(ny, nx) == 0 || reached.at<std::uint8_t>(ny, nx) != 0) {
          continue;
        }
        reached.at<std::uint8_t>(ny, nx) = 255;
        queue.push(cv::Point(nx, ny));
      }
    }
  }
  for (int y = 0; y < skeleton.rows; ++y) {
    for (int x = 0; x < skeleton.cols; ++x) {
      if (distance.at<float>(y, x) < kBandPx && reached.at<std::uint8_t>(y, x) == 0) {
        skeleton.at<std::uint8_t>(y, x) = 0;
      }
    }
  }
}

int neighborComponentCount(const cv::Mat& skeleton, int x, int y) {
  bool on[3][3] = {};
  int count = 0;
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      if (dx == 0 && dy == 0) {
        continue;
      }
      const int nx = x + dx;
      const int ny = y + dy;
      if (nx < 0 || ny < 0 || nx >= skeleton.cols || ny >= skeleton.rows) {
        continue;
      }
      if (skeleton.at<std::uint8_t>(ny, nx) != 0) {
        on[dy + 1][dx + 1] = true;
        ++count;
      }
    }
  }
  if (count < 2) {
    return 0;
  }
  bool seen[3][3] = {};
  int components = 0;
  for (int sy = 0; sy < 3; ++sy) {
    for (int sx = 0; sx < 3; ++sx) {
      if (!on[sy][sx] || seen[sy][sx] || (sx == 1 && sy == 1)) {
        continue;
      }
      ++components;
      std::queue<std::pair<int, int>> queue;
      queue.push({sx, sy});
      seen[sy][sx] = true;
      while (!queue.empty()) {
        const auto [cx, cy] = queue.front();
        queue.pop();
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            const int nx = cx + dx;
            const int ny = cy + dy;
            if (nx < 0 || ny < 0 || nx > 2 || ny > 2 || (nx == 1 && ny == 1) || !on[ny][nx] ||
                seen[ny][nx]) {
              continue;
            }
            seen[ny][nx] = true;
            queue.push({nx, ny});
          }
        }
      }
    }
  }
  return components;
}

// A 2-pixel cap or 2x2 block has no degree-1 pixel, so the vein tip never
// becomes an endpoint. Drop pixels whose neighbors are already connected.
void removeRedundantSkeletonPixels(cv::Mat& skeleton) {
  bool changed = true;
  while (changed) {
    changed = false;
    for (int y = 0; y < skeleton.rows && !changed; ++y) {
      for (int x = 0; x < skeleton.cols; ++x) {
        if (skeleton.at<std::uint8_t>(y, x) == 0) {
          continue;
        }
        if (neighborComponentCount(skeleton, x, y) == 1) {
          skeleton.at<std::uint8_t>(y, x) = 0;
          changed = true;
          break;
        }
      }
    }
  }
}

}  // namespace

void cleanupVeinSkeleton(cv::Mat& skeleton, const LeafContour& contour) {
  if (skeleton.empty() || contour.path.size() < 3) {
    return;
  }
  const cv::Mat leaf = contourMask(contour, skeleton.size());
  if (cv::countNonZero(leaf) == 0) {
    return;
  }
  suppressContourParallelRing(skeleton, contour.path, leaf);
  removeRedundantSkeletonPixels(skeleton);
}

Outcome<CenterVein> detectCenterVein(const PreprocessResult& image, const LeafContour& contour,
                                     const DetectionConfig& config) noexcept {
  try {
    if (image.gray.empty() || image.gray.type() != CV_8UC1 || contour.path.size() < 3) {
      return veinError(ErrorCode::CentralVeinNotFound, "input");
    }
    const cv::Mat leaf = contourMask(contour, image.gray.size());
    if (cv::countNonZero(leaf) == 0) {
      return veinError(ErrorCode::CentralVeinNotFound, "contour");
    }
    cv::Mat veins = ridgeMask(image.gray, ridgeDomain(leaf), config);
    if (cv::countNonZero(veins) == 0) {
      return veinError(ErrorCode::CentralVeinNotFound, "ridges");
    }
    const auto thinned = zhangSuen(veins);
    if (!thinned.hasValue()) {
      return veinError(ErrorCode::CentralVeinNotFound, "thinning");
    }
    cv::Mat skeleton = *thinned.value() * 255;
    suppressContourParallelRing(skeleton, contour.path, leaf);
    removeRedundantSkeletonPixels(skeleton);
    const auto graphResult = buildSkeletonGraph(skeleton);
    if (!graphResult.hasValue() || graphResult.value()->nodes.size() < 2) {
      return veinError(ErrorCode::CentralVeinNotFound, "graph");
    }
    const SkeletonGraph& graph = *graphResult.value();
    std::vector<int> ends;
    for (int index = 0; index < static_cast<int>(graph.nodes.size()); ++index) {
      if (graph.nodes[static_cast<std::size_t>(index)].edges.size() <= 1) {
        ends.push_back(index);
      }
    }
    if (ends.size() > 24) {
      ends.resize(24);
    }
    std::vector<PathCandidate> candidates;
    int pairId = 0;
    for (std::size_t i = 0; i < ends.size(); ++i) {
      for (std::size_t j = i + 1; j < ends.size(); ++j) {
        const std::vector<int> nodes = shortestPath(graph, ends[i], ends[j]);
        if (nodes.size() < 2) {
          continue;
        }
        PathCandidate candidate;
        candidate.points.reserve(nodes.size());
        candidate.minX = std::numeric_limits<double>::infinity();
        candidate.minY = candidate.minX;
        for (const int node : nodes) {
          const Point point = graph.nodes[static_cast<std::size_t>(node)].point;
          candidate.points.push_back(point);
          candidate.minX = std::min(candidate.minX, point.x);
          candidate.minY = std::min(candidate.minY, point.y);
        }
        candidate.length = arcLength(candidate.points);
        const double extent =
            longitudinalExtent(leaf, candidate.points.front(), candidate.points.back());
        if (!(extent > 0.0) || candidate.length / extent < config.minCenterVeinLengthRatio) {
          continue;
        }
        double offset = 0;
        const Point start = candidate.points.front();
        const Point finish = candidate.points.back();
        const Point axis{finish.x - start.x, finish.y - start.y};
        const double axisLength = std::hypot(axis.x, axis.y);
        for (const Point& point : candidate.points) {
          if (axisLength <= 1e-9) {
            break;
          }
          const double cross =
              std::abs((point.x - start.x) * axis.y - (point.y - start.y) * axis.x) / axisLength;
          offset += cross;
        }
        offset /= static_cast<double>(candidate.points.size());
        const double centrality = 1.0 / (1.0 + offset / 8.0);
        candidate.score = std::clamp(std::min(candidate.length / extent, 1.0) * centrality, 0.0, 1.0);
        candidate.id = pairId++;
        candidates.push_back(std::move(candidate));
      }
    }
    if (candidates.empty()) {
      return veinError(ErrorCode::CentralVeinNotFound, "length");
    }
    std::sort(candidates.begin(), candidates.end(), [](const PathCandidate& a, const PathCandidate& b) {
      if (a.score != b.score) {
        return a.score > b.score;
      }
      if (a.length != b.length) {
        return a.length > b.length;
      }
      if (a.minY != b.minY) {
        return a.minY < b.minY;
      }
      if (a.minX != b.minX) {
        return a.minX < b.minX;
      }
      return a.id < b.id;
    });
    if (candidates.front().score < config.minimumStageConfidence) {
      return veinError(ErrorCode::CentralVeinNotFound, "confidence");
    }
    if (candidates.size() >= 2 &&
        candidates[0].score - candidates[1].score < config.ambiguityScoreDelta) {
      return veinError(ErrorCode::AmbiguousCentralVein, "candidates");
    }

    PathCandidate best = candidates.front();
    const Point forward = {best.points.back().x - best.points.front().x,
                           best.points.back().y - best.points.front().y};
    const double frontWidth = chordWidth(leaf, best.points.front(), forward);
    const double backWidth = chordWidth(leaf, best.points.back(), forward);
    if (std::abs(frontWidth - backWidth) <= 1.0) {
      return veinError(ErrorCode::AmbiguousCentralVein, "direction");
    }
    if (backWidth > frontWidth) {
      std::reverse(best.points.begin(), best.points.end());
    }
    CenterVein vein;
    vein.path.reserve(best.points.size());
    for (const Point& point : best.points) {
      vein.path.push_back(image.transform.toSource(point));
    }
    vein.base = vein.path.front();
    vein.apex = vein.path.back();
    vein.confidence = best.score;
    if (!std::isfinite(vein.confidence) || vein.confidence < 0.0 || vein.confidence > 1.0) {
      return veinError(ErrorCode::InternalError, "confidence");
    }
    return Outcome<CenterVein>::success(std::move(vein));
  } catch (const std::bad_alloc&) {
    return veinError(ErrorCode::InternalError, "allocation");
  } catch (...) {
    return veinError(ErrorCode::InternalError, "center vein");
  }
}

}  // namespace leaf::detail
