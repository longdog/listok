#include "geometry_math.h"
#include "polyline.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace leaf::detail {
namespace {

constexpr double kDistinctPointTolerance = 1e-12;

struct ArcSamples {
  std::vector<Point> points;
  double span{0};
};

Outcome<ArcSamples> sampleArcInterval(const Path& path, double s0, double s1) noexcept {
  if (path.size() < 2) {
    return Outcome<ArcSamples>::failure(
        {ErrorCode::InvalidMeasurements, Stage::Measurements, "path too short"});
  }

  const double total = arcLength(path);
  const double start = std::clamp(s0, 0.0, total);
  const double end = std::clamp(s1, 0.0, total);
  const double span = std::abs(end - start);
  if (span <= 0.0) {
    return Outcome<ArcSamples>::failure(
        {ErrorCode::InvalidMeasurements, Stage::Measurements, "empty span"});
  }

  const double lo = std::min(start, end);
  const double hi = std::max(start, end);
  const int sampleCount = std::max(3, static_cast<int>(std::ceil(span)) + 1);

  ArcSamples samples;
  samples.span = span;
  for (int i = 0; i < sampleCount; ++i) {
    const double s = lo + (hi - lo) * static_cast<double>(i) / static_cast<double>(sampleCount - 1);
    const auto point = pointAtArc(path, s);
    if (!point.hasValue()) {
      return Outcome<ArcSamples>::failure(*point.error());
    }
    samples.points.push_back(*point.value());
  }

  return Outcome<ArcSamples>::success(std::move(samples));
}

int countDistinctPoints(const std::vector<Point>& points, double tolerance) noexcept {
  int distinct = 0;
  for (const Point& point : points) {
    bool isDistinct = true;
    for (int i = 0; i < distinct; ++i) {
      if (nearlyEqual(point, points[static_cast<std::size_t>(i)], tolerance)) {
        isDistinct = false;
        break;
      }
    }
    if (isDistinct) {
      ++distinct;
    }
  }
  return distinct;
}

Outcome<std::vector<Point>> collectPointsInArcInterval(const Path& path, double lo, double hi,
                                                         double tolerance) noexcept {
  std::vector<Point> points;
  double traveled = 0.0;
  for (std::size_t i = 0; i < path.size(); ++i) {
    if (i > 0) {
      traveled += length(subtract(path[i], path[i - 1]));
    }
    if (traveled + tolerance >= lo && traveled - tolerance <= hi) {
      points.push_back(path[i]);
    }
  }

  const auto start = pointAtArc(path, lo);
  if (!start.hasValue()) {
    return Outcome<std::vector<Point>>::failure(*start.error());
  }
  points.push_back(*start.value());

  const auto end = pointAtArc(path, hi);
  if (!end.hasValue()) {
    return Outcome<std::vector<Point>>::failure(*end.error());
  }
  points.push_back(*end.value());

  return Outcome<std::vector<Point>>::success(std::move(points));
}

bool hasAtLeastThreeDistinctPoints(const std::vector<Point>& points) noexcept {
  int distinct = 0;
  for (const Point& point : points) {
    bool isDistinct = true;
    for (int i = 0; i < distinct; ++i) {
      if (nearlyEqual(point, points[static_cast<std::size_t>(i)], kDistinctPointTolerance)) {
        isDistinct = false;
        break;
      }
    }
    if (isDistinct) {
      ++distinct;
      if (distinct >= 3) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

double arcLength(const Path& path) noexcept {
  if (path.size() < 2) {
    return 0.0;
  }
  double total = 0.0;
  for (std::size_t i = 1; i < path.size(); ++i) {
    total += length(subtract(path[i], path[i - 1]));
  }
  return total;
}

Outcome<Point> pointAtArc(const Path& path, double s) noexcept {
  if (path.empty()) {
    return Outcome<Point>::failure(
        {ErrorCode::InvalidMeasurements, Stage::Measurements, "empty path"});
  }
  if (s < 0.0) {
    return Outcome<Point>::failure(
        {ErrorCode::InvalidMeasurements, Stage::Measurements, "negative arc"});
  }

  const double total = arcLength(path);
  if (s > total) {
    return Outcome<Point>::failure(
        {ErrorCode::InvalidMeasurements, Stage::Measurements, "arc beyond path"});
  }
  if (path.size() == 1 || s == 0.0) {
    return Outcome<Point>::success(path.front());
  }
  if (nearlyEqual(s, total, kDistinctPointTolerance)) {
    return Outcome<Point>::success(path.back());
  }

  double traveled = 0.0;
  for (std::size_t i = 1; i < path.size(); ++i) {
    const Point segment = subtract(path[i], path[i - 1]);
    const double segmentLength = length(segment);
    if (segmentLength == 0.0) {
      continue;
    }
    if (traveled + segmentLength >= s) {
      const double remaining = s - traveled;
      const double t = remaining / segmentLength;
      return Outcome<Point>::success(add(path[i - 1], multiply(segment, t)));
    }
    traveled += segmentLength;
  }

  return Outcome<Point>::success(path.back());
}

Outcome<Point> unitTangentRegression(const Path& path, double s0, double s1,
                                       double minSpan) noexcept {
  const auto samples = sampleArcInterval(path, s0, s1);
  if (!samples.hasValue()) {
    return Outcome<Point>::failure(*samples.error());
  }
  if (samples.value()->span < minSpan) {
    return Outcome<Point>::failure(
        {ErrorCode::InvalidMeasurements, Stage::Measurements, "span too small"});
  }
  const double lo = std::min(s0, s1);
  const double hi = std::max(s0, s1);
  const auto intervalPoints = collectPointsInArcInterval(path, lo, hi, kDistinctPointTolerance);
  if (!intervalPoints.hasValue()) {
    return Outcome<Point>::failure(*intervalPoints.error());
  }
  if (countDistinctPoints(*intervalPoints.value(), kDistinctPointTolerance) < 3) {
    return Outcome<Point>::failure(
        {ErrorCode::InvalidMeasurements, Stage::Measurements, "insufficient points"});
  }
  if (!hasAtLeastThreeDistinctPoints(samples.value()->points)) {
    return Outcome<Point>::failure(
        {ErrorCode::InvalidMeasurements, Stage::Measurements, "insufficient samples"});
  }

  const auto& points = samples.value()->points;
  Point centroid{0, 0};
  for (const Point& point : points) {
    centroid.x += point.x;
    centroid.y += point.y;
  }
  centroid.x /= static_cast<double>(points.size());
  centroid.y /= static_cast<double>(points.size());

  double cxx = 0.0;
  double cxy = 0.0;
  double cyy = 0.0;
  for (const Point& point : points) {
    const double dx = point.x - centroid.x;
    const double dy = point.y - centroid.y;
    cxx += dx * dx;
    cxy += dx * dy;
    cyy += dy * dy;
  }

  const double trace = cxx + cyy;
  const double determinant = cxx * cyy - cxy * cxy;
  const double discriminant = std::sqrt(std::max(0.0, trace * trace - 4.0 * determinant));
  const double lambda = 0.5 * (trace + discriminant);

  Point direction;
  if (std::abs(cxy) > kDistinctPointTolerance) {
    direction = {lambda - cyy, cxy};
  } else if (cxx >= cyy) {
    direction = {1.0, 0.0};
  } else {
    direction = {0.0, 1.0};
  }

  const auto unit = normalizeVector(direction, kDistinctPointTolerance);
  if (!unit.hasValue()) {
    return Outcome<Point>::failure(*unit.error());
  }

  const Point progression = subtract(points.back(), points.front());
  if (dot(*unit.value(), progression) < 0.0) {
    return Outcome<Point>::success(multiply(*unit.value(), -1.0));
  }
  return unit;
}

}  // namespace leaf::detail
