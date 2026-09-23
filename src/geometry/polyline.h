#pragma once

#include <leaf/outcome.h>
#include <leaf/types.h>

namespace leaf::detail {

Outcome<Point> pointAtArc(const Path& path, double s) noexcept;
double arcLength(const Path& path) noexcept;
Outcome<Point> unitTangentRegression(const Path& path, double s0, double s1,
                                     double minSpan) noexcept;

}  // namespace leaf::detail
