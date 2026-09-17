#pragma once

#include <leaf/config.h>
#include <leaf/image.h>

#include <opencv2/core.hpp>

namespace leaf::detail {

ResizeTransform computeResizeTransform(std::int32_t sourceWidth, std::int32_t sourceHeight,
                                       int targetMaxDimension) noexcept;

cv::Mat resizeMat(const cv::Mat& source, const ResizeTransform& transform);

}  // namespace leaf::detail
