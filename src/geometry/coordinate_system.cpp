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
