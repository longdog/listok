#pragma once

#include <leaf/outcome.h>
#include <leaf/types.h>

namespace leaf::detail {

Outcome<CoordinateSystem> makeCoordinateSystem(const CenterVein& vein) noexcept;

}  // namespace leaf::detail
