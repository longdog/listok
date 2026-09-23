#pragma once

#include <leaf/outcome.h>
#include <leaf/types.h>

#include <utility>

namespace leaf::detail {

Outcome<std::pair<Point, Point>> contourRayHits(const Path& contour, Point origin,
                                                Point unitXAxis) noexcept;

}  // namespace leaf::detail
