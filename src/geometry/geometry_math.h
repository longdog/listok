#pragma once

#include <leaf/outcome.h>
#include <leaf/types.h>

#include <cmath>

namespace leaf::detail {

inline double dot(Point a, Point b) noexcept { return a.x * b.x + a.y * b.y; }

inline double cross(Point a, Point b) noexcept { return a.x * b.y - a.y * b.x; }

inline double length(Point v) noexcept { return std::hypot(v.x, v.y); }

inline Point subtract(Point a, Point b) noexcept { return {a.x - b.x, a.y - b.y}; }

inline Point add(Point a, Point b) noexcept { return {a.x + b.x, a.y + b.y}; }

inline Point multiply(Point v, double factor) noexcept { return {v.x * factor, v.y * factor}; }

inline bool nearlyEqual(double a, double b, double tolerance) noexcept {
  return std::abs(a - b) <= tolerance;
}

inline bool nearlyEqual(Point a, Point b, double tolerance) noexcept {
  return nearlyEqual(a.x, b.x, tolerance) && nearlyEqual(a.y, b.y, tolerance);
}

inline int signNonZero(double value, double tolerance) noexcept {
  if (value > tolerance) {
    return 1;
  }
  if (value < -tolerance) {
    return -1;
  }
  return 0;
}

inline Outcome<Point> normalizeVector(Point v, double tolerance) noexcept {
  const double len = length(v);
  if (len <= tolerance) {
    return Outcome<Point>::failure(
        {ErrorCode::InvalidMeasurements, Stage::Measurements, "zero vector"});
  }
  return Outcome<Point>::success({v.x / len, v.y / len});
}

}  // namespace leaf::detail
