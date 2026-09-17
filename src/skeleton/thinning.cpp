#include "thinning.h"

#include <new>
#include <utility>
#include <vector>

namespace leaf::detail {
namespace {

constexpr int kNeighborDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
constexpr int kNeighborDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};

Outcome<cv::Mat> skeletonFailure(const char* message) {
  return Outcome<cv::Mat>::failure(
      {ErrorCode::SkeletonizationFailed, Stage::Skeleton, message});
}

bool isBinaryU8(const cv::Mat& mat) {
  return mat.dims == 2 && !mat.empty() && mat.type() == CV_8UC1;
}

uchar neighborValue(const cv::Mat& img, int y, int x) {
  if (y < 0 || x < 0 || y >= img.rows || x >= img.cols) {
    return 0;
  }
  return img.at<uchar>(y, x);
}

int neighborTransitions(const uchar neighbors[8]) {
  int transitions = 0;
  for (int i = 0; i < 8; ++i) {
    if (neighbors[i] == 0 && neighbors[(i + 1) % 8] != 0) {
      ++transitions;
    }
  }
  return transitions;
}

int neighborCount(const uchar neighbors[8]) {
  int count = 0;
  for (int i = 0; i < 8; ++i) {
    count += neighbors[i] != 0 ? 1 : 0;
  }
  return count;
}

std::size_t collectDeletions(const cv::Mat& img, int subiteration,
                             std::vector<std::pair<int, int>>& deletions) {
  deletions.clear();
  if (img.rows < 3 || img.cols < 3) {
    return 0;
  }

  for (int y = 1; y < img.rows - 1; ++y) {
    for (int x = 1; x < img.cols - 1; ++x) {
      if (img.at<uchar>(y, x) == 0) {
        continue;
      }

      uchar neighbors[8];
      for (int i = 0; i < 8; ++i) {
        neighbors[i] = neighborValue(img, y + kNeighborDy[i], x + kNeighborDx[i]);
      }

      const int b = neighborCount(neighbors);
      if (b < 2 || b > 6 || neighborTransitions(neighbors) != 1) {
        continue;
      }

      const uchar p2 = neighbors[0];
      const uchar p4 = neighbors[2];
      const uchar p6 = neighbors[4];
      const uchar p8 = neighbors[6];
      const bool deletePixel = subiteration == 0 ? (p2 * p4 * p6 == 0 && p4 * p6 * p8 == 0)
                                                 : (p2 * p4 * p8 == 0 && p2 * p6 * p8 == 0);
      if (deletePixel) {
        deletions.emplace_back(y, x);
      }
    }
  }
  return deletions.size();
}

void applyDeletions(cv::Mat& img, const std::vector<std::pair<int, int>>& deletions) {
  for (const auto& [y, x] : deletions) {
    img.at<uchar>(y, x) = 0;
  }
}

cv::Mat normalizeBinary(const cv::Mat& input) {
  cv::Mat binary(input.size(), CV_8UC1);
  for (int y = 0; y < input.rows; ++y) {
    const uchar* src = input.ptr<uchar>(y);
    uchar* dst = binary.ptr<uchar>(y);
    for (int x = 0; x < input.cols; ++x) {
      dst[x] = src[x] != 0 ? 1 : 0;
    }
  }
  return binary;
}

}  // namespace

Outcome<cv::Mat> zhangSuen(const cv::Mat& binary) noexcept {
  try {
    if (!isBinaryU8(binary)) {
      return skeletonFailure("invalid binary image");
    }

    cv::Mat img = normalizeBinary(binary);
    std::vector<std::pair<int, int>> deletions;
    while (true) {
      const std::size_t first = collectDeletions(img, 0, deletions);
      applyDeletions(img, deletions);
      const std::size_t second = collectDeletions(img, 1, deletions);
      applyDeletions(img, deletions);
      if (first == 0 && second == 0) {
        break;
      }
    }
    return Outcome<cv::Mat>::success(std::move(img));
  } catch (const cv::Exception&) {
    return skeletonFailure("opencv");
  } catch (const std::bad_alloc&) {
    return Outcome<cv::Mat>::failure(
        {ErrorCode::InternalError, Stage::Skeleton, "allocation"});
  } catch (...) {
    return Outcome<cv::Mat>::failure(
        {ErrorCode::InternalError, Stage::Skeleton, "unknown"});
  }
}

}  // namespace leaf::detail
