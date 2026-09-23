#include "resize.h"

#include <algorithm>
#include <cmath>

#include <opencv2/imgproc.hpp>

namespace leaf::detail {

ResizeTransform computeResizeTransform(std::int32_t sourceWidth, std::int32_t sourceHeight,
                                       int targetMaxDimension) noexcept {
  ResizeTransform transform{};
  transform.sourceWidth = sourceWidth;
  transform.sourceHeight = sourceHeight;
  transform.workingWidth = sourceWidth;
  transform.workingHeight = sourceHeight;
  transform.sourcePerWorkingX = 1.0;
  transform.sourcePerWorkingY = 1.0;

  const int maxDim = std::max(sourceWidth, sourceHeight);
  if (maxDim <= targetMaxDimension) {
    return transform;
  }

  const double scale = static_cast<double>(targetMaxDimension) / static_cast<double>(maxDim);
  transform.workingWidth =
      static_cast<std::int32_t>(std::lround(static_cast<double>(sourceWidth) * scale));
  transform.workingHeight =
      static_cast<std::int32_t>(std::lround(static_cast<double>(sourceHeight) * scale));
  transform.workingWidth = std::max(transform.workingWidth, 1);
  transform.workingHeight = std::max(transform.workingHeight, 1);
  transform.sourcePerWorkingX =
      static_cast<double>(sourceWidth) / static_cast<double>(transform.workingWidth);
  transform.sourcePerWorkingY =
      static_cast<double>(sourceHeight) / static_cast<double>(transform.workingHeight);
  return transform;
}

cv::Mat resizeMat(const cv::Mat& source, const ResizeTransform& transform) {
  if (source.cols == transform.workingWidth && source.rows == transform.workingHeight) {
    return source;
  }
  cv::Mat resized;
  cv::resize(source, resized,
             cv::Size(transform.workingWidth, transform.workingHeight), 0, 0, cv::INTER_AREA);
  return resized;
}

}  // namespace leaf::detail
