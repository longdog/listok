#include "geometry_math.h"
#include "intersection.h"
#include "polyline.h"

#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace leaf::detail {
namespace {

constexpr double kVertexTolerance = 1e-9;

struct RayHit {
  Point point;
  double distance{0};
  int direction{0};
};

bool segmentRayIntersection(Point origin, Point direction, Point a, Point b, double parallelEpsilon,
                            Point& hit, double& distance) noexcept {
  const Point segment = subtract(b, a);
  const double denominator = cross(direction, segment);
  if (std::abs(denominator) <= parallelEpsilon) {
    return false;
  }

  const Point delta = subtract(a, origin);
  const double t = cross(delta, segment) / denominator;
  const double u = cross(delta, direction) / denominator;
  if (t < 0.0 || u < -kVertexTolerance || u > 1.0 + kVertexTolerance) {
    return false;
  }

  hit = add(origin, multiply(direction, t));
  distance = t;
  return true;
}

bool isDuplicateHit(const std::vector<RayHit>& hits, const Point& candidate,
                    double tolerance) noexcept {
  for (const RayHit& hit : hits) {
    if (nearlyEqual(hit.point, candidate, tolerance)) {
      return true;
    }
  }
  return false;
}

std::optional<RayHit> nearestHit(const std::vector<RayHit>& hits, int direction) noexcept {
  std::optional<RayHit> nearest;
  for (const RayHit& hit : hits) {
    if (hit.direction != direction) {
      continue;
    }
    if (!nearest.has_value() || hit.distance < nearest->distance) {
      nearest = hit;
    }
  }
  return nearest;
}

}  // namespace

Outcome<std::pair<Point, Point>> contourRayHits(const Path& contour, Point origin,
                                                  Point unitXAxis) noexcept {
  if (contour.size() < 2) {
    return Outcome<std::pair<Point, Point>>::failure(
        {ErrorCode::InvalidMeasurements, Stage::Measurements, "contour too short"});
  }

  const auto axis = normalizeVector(unitXAxis, kVertexTolerance);
  if (!axis.hasValue()) {
    return Outcome<std::pair<Point, Point>>::failure(*axis.error());
  }

  const double contourScale = std::max(arcLength(contour), 1.0);
  const double parallelEpsilon = 1e-12 * contourScale;
  const double dedupeTolerance = 1e-9 * contourScale;

  std::vector<RayHit> hits;
  for (std::size_t i = 1; i < contour.size(); ++i) {
    for (int direction : {-1, 1}) {
      Point hit{};
      double distance = 0.0;
      const Point rayDirection = multiply(*axis.value(), static_cast<double>(direction));
      if (!segmentRayIntersection(origin, rayDirection, contour[i - 1], contour[i],
                                  parallelEpsilon, hit, distance)) {
        continue;
      }
      if (isDuplicateHit(hits, hit, dedupeTolerance)) {
        continue;
      }
      hits.push_back({hit, distance, direction});
    }
  }

  const auto negative = nearestHit(hits, -1);
  const auto positive = nearestHit(hits, 1);
  if (!negative.has_value() || !positive.has_value()) {
    return Outcome<std::pair<Point, Point>>::failure(
        {ErrorCode::InvalidMeasurements, Stage::Measurements, "missing ray hit"});
  }

  return Outcome<std::pair<Point, Point>>::success({negative->point, positive->point});
}

}  // namespace leaf::detail
