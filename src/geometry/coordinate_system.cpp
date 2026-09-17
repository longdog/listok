#include "coordinate_system.h"
#include "geometry_math.h"
#include "polyline.h"

#include <leaf/types.h>

namespace leaf {

Point CoordinateSystem::imageToNormalized(Point p) const noexcept {
  const double dx = p.x - origin.x;
  const double dy = p.y - origin.y;
  const double normalizedX = (dx * xAxis.x + dy * xAxis.y) / scale;
  const double normalizedY = (dx * yAxis.x + dy * yAxis.y) / scale;
  return {normalizedX, normalizedY};
}

Point CoordinateSystem::normalizedToImage(Point p) const noexcept {
  const double imageX = origin.x + (p.x * xAxis.x + p.y * yAxis.x) * scale;
  const double imageY = origin.y + (p.x * xAxis.y + p.y * yAxis.y) * scale;
  return {imageX, imageY};
}

}  // namespace leaf

namespace leaf::detail {

Outcome<CoordinateSystem> makeCoordinateSystem(const CenterVein& vein) noexcept {
  const double scale = arcLength(vein.path);
  if (scale <= 1e-12) {
    return Outcome<CoordinateSystem>::failure(
        {ErrorCode::InvalidCoordinateSystem, Stage::CoordinateSystem, "zero scale"});
  }

  const Point yRaw = subtract(vein.apex, vein.base);
  const auto yAxis = normalizeVector(yRaw, 1e-12);
  if (!yAxis.hasValue()) {
    return Outcome<CoordinateSystem>::failure(
        {ErrorCode::InvalidCoordinateSystem, Stage::CoordinateSystem, "degenerate y axis"});
  }

  const Point xAxis{yAxis.value()->y, -yAxis.value()->x};
  return Outcome<CoordinateSystem>::success(
      CoordinateSystem{vein.base, xAxis, *yAxis.value(), scale});
}

}  // namespace leaf::detail
