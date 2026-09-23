#pragma once

#include <leaf/outcome.h>
#include <leaf/types.h>

namespace leaf::detail {

Outcome<LeafKeypoints> extractKeypoints(const LeafContour& contour, const CenterVein& center,
                                        const SecondaryVeins& veins,
                                        const CoordinateSystem& cs) noexcept;

}  // namespace leaf::detail
