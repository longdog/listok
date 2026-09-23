#pragma once

#include <leaf/config.h>
#include <leaf/outcome.h>
#include <leaf/types.h>

#include "../../preprocess/preprocessor.h"

namespace leaf::detail {

Outcome<CenterVein> detectCenterVein(const PreprocessResult& image, const LeafContour& contour,
                                     const DetectionConfig& config) noexcept;

// Drop a contour-hugging skeleton ring and pixels that only close a tip into
// a loop. A thin shape with no interior skeleton is left unchanged.
void cleanupVeinSkeleton(cv::Mat& skeleton, const LeafContour& contour);

}  // namespace leaf::detail
