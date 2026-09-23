#pragma once

#include <leaf/config.h>
#include <leaf/outcome.h>
#include <leaf/types.h>

#include "../../skeleton/graph.h"

namespace leaf::detail {

Outcome<SecondaryVeins> detectSecondaryVeins(const SkeletonGraph& graph, const LeafContour& contour,
                                             const CenterVein& center, const CoordinateSystem& cs,
                                             const DetectionConfig& config) noexcept;

}  // namespace leaf::detail
