#pragma once

#include <leaf/outcome.h>
#include <leaf/types.h>

namespace leaf::detail {

Outcome<LeafMeasurements> extractMeasurements(const LeafContour& contour,
                                                const CenterVein& center,
                                                const SecondaryVeins& veins,
                                                const LeafKeypoints& keypoints,
                                                const CoordinateSystem& cs) noexcept;

}  // namespace leaf::detail
