#pragma once

#include <leaf/outcome.h>
#include <leaf/types.h>

namespace leaf::detail {

Outcome<double> sameSideContourArc(const Path& contour, Point from, Point to,
                                   const CoordinateSystem& cs, Point baseSeparator,
                                   Point apexSeparator, double equalityTolerance) noexcept;
double unorientedAngleDegrees(Point a, Point b) noexcept;

}  // namespace leaf::detail
