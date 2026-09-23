#pragma once

#include <leaf/outcome.h>

#include <opencv2/core.hpp>

namespace leaf::detail {

// Binary Zhang–Suen thinning. Nonzero input pixels are treated as foreground and
// normalized to {0,1}. The input matrix is not modified. Outside the image is
// background. Only interior pixels (not on the image border) are candidates.
Outcome<cv::Mat> zhangSuen(const cv::Mat& binary) noexcept;

}  // namespace leaf::detail
