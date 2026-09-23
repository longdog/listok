#include "extractor.h"

#include "../geometry/contour_arc.h"
#include "../geometry/geometry_math.h"
#include "../geometry/intersection.h"
#include "../geometry/polyline.h"

#include <algorithm>
#include <cmath>

namespace leaf::detail {
namespace {

constexpr double kEqualityScale = 1e-9;

Outcome<LeafMeasurements> invalidCoordinateSystem(const char* message) noexcept {
  return Outcome<LeafMeasurements>::failure(
      {ErrorCode::InvalidCoordinateSystem, Stage::Measurements, message});
}

Outcome<LeafMeasurements> invalidMeasurements(const char* message) noexcept {
  return Outcome<LeafMeasurements>::failure(
      {ErrorCode::InvalidMeasurements, Stage::Measurements, message});
}

bool isFinitePoint(Point point) noexcept {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

Outcome<double> arcCoordinateOfPoint(const Path& path, Point point) noexcept {
  if (path.empty()) {
    return Outcome<double>::failure(
        {ErrorCode::InvalidMeasurements, Stage::Measurements, "empty path"});
  }

  const double tolerance = std::max(1e-12, kEqualityScale);
  double traveled = 0.0;
  for (std::size_t i = 1; i < path.size(); ++i) {
    const Point segment = subtract(path[i], path[i - 1]);
    const double segmentLength = length(segment);
    if (segmentLength <= tolerance) {
      if (nearlyEqual(path[i - 1], point, tolerance) || nearlyEqual(path[i], point, tolerance)) {
        return Outcome<double>::success(traveled);
      }
      continue;
    }

    const Point delta = subtract(point, path[i - 1]);
    const double projection = dot(delta, segment) / segmentLength;
    if (projection < -tolerance || projection > segmentLength + tolerance) {
      traveled += segmentLength;
      continue;
    }

    const Point projected = add(path[i - 1], multiply(segment, projection / segmentLength));
    if (nearlyEqual(projected, point, tolerance)) {
      return Outcome<double>::success(traveled + std::clamp(projection, 0.0, segmentLength));
    }
    traveled += segmentLength;
  }

  if (nearlyEqual(path.back(), point, tolerance)) {
    return Outcome<double>::success(arcLength(path));
  }

  // Skeleton attachments are pixel centers adjacent to the center polyline.
  // Snap a point that lies within one pixel; exact points already returned above.
  constexpr double kPixelSnap = 8.0;
  traveled = 0.0;
  double bestDistance = kPixelSnap;
  double bestArc = 0.0;
  bool found = false;
  for (std::size_t i = 1; i < path.size(); ++i) {
    const Point segment = subtract(path[i], path[i - 1]);
    const double segmentLength = length(segment);
    if (segmentLength <= tolerance) {
      const double distance = length(subtract(point, path[i - 1]));
      if (distance <= bestDistance) {
        bestDistance = distance;
        bestArc = traveled;
        found = true;
      }
      continue;
    }
    const Point delta = subtract(point, path[i - 1]);
    const double projection = std::clamp(dot(delta, segment) / segmentLength, 0.0, segmentLength);
    const Point projected = add(path[i - 1], multiply(segment, projection / segmentLength));
    const double distance = length(subtract(projected, point));
    if (distance <= bestDistance) {
      bestDistance = distance;
      bestArc = traveled + projection;
      found = true;
    }
    traveled += segmentLength;
  }
  if (found) {
    return Outcome<double>::success(bestArc);
  }

  return Outcome<double>::failure(
      {ErrorCode::InvalidMeasurements, Stage::Measurements, "point not on path"});
}

bool isValidNormalizedLength(double value) noexcept {
  return std::isfinite(value) && value >= 0.0;
}

bool isValidAngle(double degrees) noexcept {
  return std::isfinite(degrees) && degrees >= 0.0 && degrees <= 180.0;
}

Outcome<SideMeasurements> measureSide(const Vein& firstVein, const Vein& secondVein,
                                      const Path& contour, const Path& centerPath,
                                      const CoordinateSystem& cs, Point f, Point base, Point apex,
                                      bool measureLeft) noexcept {
  SideMeasurements side{};

  const auto hits = contourRayHits(contour, f, cs.xAxis);
  if (!hits.hasValue()) {
    return Outcome<SideMeasurements>::failure(*hits.error());
  }

  const Point hit = measureLeft ? hits.value()->first : hits.value()->second;
  side.m1 = length(subtract(f, hit)) / cs.scale;
  if (!isValidNormalizedLength(side.m1)) {
    return Outcome<SideMeasurements>::failure(
        {ErrorCode::InvalidMeasurements, Stage::Measurements, "invalid m1"});
  }

  side.m2 = arcLength(secondVein.path) / cs.scale;
  if (!isValidNormalizedLength(side.m2)) {
    return Outcome<SideMeasurements>::failure(
        {ErrorCode::InvalidMeasurements, Stage::Measurements, "invalid m2"});
  }

  const auto firstAttachment = arcCoordinateOfPoint(centerPath, firstVein.attachment);
  if (!firstAttachment.hasValue()) {
    return Outcome<SideMeasurements>::failure(*firstAttachment.error());
  }
  const auto secondAttachment = arcCoordinateOfPoint(centerPath, secondVein.attachment);
  if (!secondAttachment.hasValue()) {
    return Outcome<SideMeasurements>::failure(*secondAttachment.error());
  }
  side.m3 = std::abs(*secondAttachment.value() - *firstAttachment.value()) / cs.scale;
  if (!isValidNormalizedLength(side.m3)) {
    return Outcome<SideMeasurements>::failure(
        {ErrorCode::InvalidMeasurements, Stage::Measurements, "invalid m3"});
  }

  const auto snapToContour = [&contour](Point point) {
    Point best = point;
    double bestDistance = 8.0;
    bool found = false;
    for (const Point& vertex : contour) {
      const double distance = length(subtract(vertex, point));
      if (distance <= bestDistance && (!found || distance < bestDistance)) {
        bestDistance = distance;
        best = vertex;
        found = true;
      }
    }
    return found ? best : point;
  };
  const auto contourArc = sameSideContourArc(contour, snapToContour(firstVein.endpoint),
                                             snapToContour(secondVein.endpoint), cs, base, apex,
                                             kEqualityScale * cs.scale);
  if (!contourArc.hasValue()) {
    return Outcome<SideMeasurements>::failure(*contourArc.error());
  }
  side.m4 = *contourArc.value() / cs.scale;
  if (!isValidNormalizedLength(side.m4)) {
    return Outcome<SideMeasurements>::failure(
        {ErrorCode::InvalidMeasurements, Stage::Measurements, "invalid m4"});
  }

  const double window = 0.02 * cs.scale;
  const auto centerTangent =
      unitTangentRegression(centerPath, *secondAttachment.value() - window,
                            *secondAttachment.value() + window, 0.5 * window);
  if (!centerTangent.hasValue()) {
    return Outcome<SideMeasurements>::failure(*centerTangent.error());
  }

  const auto secondaryTangent = unitTangentRegression(secondVein.path, 0.0, window, 0.5 * window);
  if (!secondaryTangent.hasValue()) {
    return Outcome<SideMeasurements>::failure(*secondaryTangent.error());
  }

  side.m5 = unorientedAngleDegrees(*centerTangent.value(), *secondaryTangent.value());
  if (!isValidAngle(side.m5)) {
    return Outcome<SideMeasurements>::failure(
        {ErrorCode::InvalidMeasurements, Stage::Measurements, "invalid m5"});
  }

  return Outcome<SideMeasurements>::success(side);
}

}  // namespace

Outcome<LeafMeasurements> extractMeasurements(const LeafContour& contour,
                                              const CenterVein& center,
                                              const SecondaryVeins& veins,
                                              const LeafKeypoints& keypoints,
                                              const CoordinateSystem& cs) noexcept {
  if (!std::isfinite(cs.scale) || cs.scale <= 0.0) {
    return invalidCoordinateSystem("invalid scale");
  }
  if (!isFinitePoint(cs.origin) || !isFinitePoint(cs.xAxis) || !isFinitePoint(cs.yAxis)) {
    return invalidCoordinateSystem("nonfinite axes");
  }
  if (length(cs.xAxis) <= kEqualityScale || length(cs.yAxis) <= kEqualityScale) {
    return invalidCoordinateSystem("degenerate axes");
  }

  const double centerLength = arcLength(center.path);
  if (center.path.size() < 2 || centerLength <= kEqualityScale) {
    return invalidMeasurements("degenerate center vein");
  }
  if (contour.path.size() < 2) {
    return invalidMeasurements("degenerate contour");
  }

  const auto f = pointAtArc(center.path, 0.5 * centerLength);
  if (!f.hasValue()) {
    return Outcome<LeafMeasurements>::failure(*f.error());
  }

  const auto left = measureSide(veins.leftFirst, veins.leftSecond, contour.path, center.path, cs,
                                *f.value(), center.base, center.apex, true);
  if (!left.hasValue()) {
    return Outcome<LeafMeasurements>::failure(*left.error());
  }

  const auto right = measureSide(veins.rightFirst, veins.rightSecond, contour.path, center.path, cs,
                                 *f.value(), center.base, center.apex, false);
  if (!right.hasValue()) {
    return Outcome<LeafMeasurements>::failure(*right.error());
  }

  LeafMeasurements measurements{};
  measurements.left = *left.value();
  measurements.right = *right.value();
  measurements.confidence =
      std::min({contour.confidence, center.confidence, veins.confidence, keypoints.confidence});
  return Outcome<LeafMeasurements>::success(std::move(measurements));
}

}  // namespace leaf::detail
