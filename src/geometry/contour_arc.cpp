#include "contour_arc.h"
#include "geometry_math.h"
#include "polyline.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace leaf::detail {
namespace {

constexpr double kVertexMatchTolerance = 1e-9;

struct ContourArc {
  double length{0};
  bool valid{false};
};

bool segmentsIntersectInterior(Point a, Point b, Point c, Point d, double epsilon) noexcept {
  const Point r = subtract(b, a);
  const Point s = subtract(d, c);
  const double denominator = cross(r, s);
  const Point qp = subtract(c, a);

  if (std::abs(denominator) <= epsilon) {
    return false;
  }

  const double t = cross(qp, s) / denominator;
  const double u = cross(qp, r) / denominator;
  return t > epsilon && t < 1.0 - epsilon && u > epsilon && u < 1.0 - epsilon;
}

bool crossesSeparator(Point a, Point b, Point baseSeparator, Point apexSeparator,
                      double epsilon) noexcept {
  return segmentsIntersectInterior(a, b, baseSeparator, apexSeparator, epsilon);
}

int normalizedXSign(const CoordinateSystem& cs, Point point, double tolerance) noexcept {
  return signNonZero(cs.imageToNormalized(point).x, tolerance);
}

bool pointOnSameSide(int requiredSign, const CoordinateSystem& cs, Point point,
                     double tolerance) noexcept {
  if (requiredSign == 0) {
    return false;
  }
  const int sign = normalizedXSign(cs, point, tolerance);
  return sign == 0 || sign == requiredSign;
}

bool arcMaintainsSide(const std::vector<Point>& vertices, const CoordinateSystem& cs,
                      int requiredSign, double tolerance) noexcept {
  for (const Point& vertex : vertices) {
    if (!pointOnSameSide(requiredSign, cs, vertex, tolerance)) {
      return false;
    }
  }
  for (std::size_t i = 1; i < vertices.size(); ++i) {
    const Point midpoint{
        0.5 * (vertices[i - 1].x + vertices[i].x),
        0.5 * (vertices[i - 1].y + vertices[i].y),
    };
    if (!pointOnSameSide(requiredSign, cs, midpoint, tolerance)) {
      return false;
    }
  }
  return true;
}

bool arcCrossesSeparator(const std::vector<Point>& vertices, Point baseSeparator,
                         Point apexSeparator, double epsilon) noexcept {
  for (std::size_t i = 1; i < vertices.size(); ++i) {
    if (crossesSeparator(vertices[i - 1], vertices[i], baseSeparator, apexSeparator, epsilon)) {
      return true;
    }
  }
  return false;
}

double polylineLength(const std::vector<Point>& vertices) noexcept {
  double total = 0.0;
  for (std::size_t i = 1; i < vertices.size(); ++i) {
    total += length(subtract(vertices[i], vertices[i - 1]));
  }
  return total;
}

std::vector<std::size_t> findVertexIndices(const Path& contour, Point point,
                                             double tolerance) noexcept {
  std::vector<std::size_t> indices;
  for (std::size_t i = 0; i < contour.size(); ++i) {
    if (nearlyEqual(contour[i], point, tolerance)) {
      indices.push_back(i);
    }
  }
  return indices;
}

ContourArc buildArc(const Path& contour, std::size_t fromIndex, std::size_t toIndex, int step,
                    const CoordinateSystem& cs, Point baseSeparator, Point apexSeparator,
                    double tolerance) noexcept {
  if (contour.size() < 2 || fromIndex == toIndex) {
    return {};
  }

  const int requiredSign = normalizedXSign(cs, contour[fromIndex], tolerance);
  std::vector<Point> vertices;
  vertices.push_back(contour[fromIndex]);

  const std::size_t n = contour.size();
  std::size_t index = fromIndex;
  while (index != toIndex) {
    const std::size_t next = (index + static_cast<std::size_t>(step) + n) % n;
    vertices.push_back(contour[next]);
    index = next;
  }

  ContourArc arc;
  arc.length = polylineLength(vertices);
  arc.valid = arcMaintainsSide(vertices, cs, requiredSign, tolerance) &&
              !arcCrossesSeparator(vertices, baseSeparator, apexSeparator, tolerance);
  return arc;
}

}  // namespace

Outcome<double> sameSideContourArc(const Path& contour, Point from, Point to,
                                     const CoordinateSystem& cs, Point baseSeparator,
                                     Point apexSeparator, double equalityTolerance) noexcept {
  if (contour.size() < 2) {
    return Outcome<double>::failure(
        {ErrorCode::InvalidMeasurements, Stage::Measurements, "contour too short"});
  }

  const double tolerance = std::max(1e-12 * cs.scale, kVertexMatchTolerance);
  const auto fromIndices = findVertexIndices(contour, from, tolerance);
  const auto toIndices = findVertexIndices(contour, to, tolerance);
  if (fromIndices.empty() || toIndices.empty()) {
    return Outcome<double>::failure(
        {ErrorCode::InvalidMeasurements, Stage::Measurements, "endpoint not on contour"});
  }

  const int fromSign = normalizedXSign(cs, from, tolerance);
  const int toSign = normalizedXSign(cs, to, tolerance);
  if (fromSign == 0 || toSign == 0 || fromSign != toSign) {
    return Outcome<double>::failure(
        {ErrorCode::InvalidMeasurements, Stage::Measurements, "opposite side endpoints"});
  }

  std::vector<double> validLengths;
  for (std::size_t fromIndex : fromIndices) {
    for (std::size_t toIndex : toIndices) {
      if (fromIndex == toIndex) {
        validLengths.push_back(0.0);
        continue;
      }
      for (int step : {1, -1}) {
        const ContourArc arc =
            buildArc(contour, fromIndex, toIndex, step, cs, baseSeparator, apexSeparator, tolerance);
        if (arc.valid) {
          validLengths.push_back(arc.length);
        }
      }
    }
  }

  if (validLengths.empty()) {
    return Outcome<double>::failure(
        {ErrorCode::InvalidMeasurements, Stage::Measurements, "no valid arc"});
  }

  const double minimum =
      *std::min_element(validLengths.begin(), validLengths.end());
  int equalMinimumCount = 0;
  for (double arcLength : validLengths) {
    if (nearlyEqual(arcLength, minimum, equalityTolerance)) {
      ++equalMinimumCount;
    }
  }
  if (equalMinimumCount >= 2) {
    return Outcome<double>::failure(
        {ErrorCode::InvalidMeasurements, Stage::Measurements, "ambiguous arc"});
  }

  return Outcome<double>::success(minimum);
}

double unorientedAngleDegrees(Point a, Point b) noexcept {
  const double lenA = length(a);
  const double lenB = length(b);
  if (lenA <= 1e-12 || lenB <= 1e-12) {
    return 0.0;
  }
  const double cosine = std::clamp(dot(a, b) / (lenA * lenB), -1.0, 1.0);
  return std::acos(cosine) * 180.0 / 3.14159265358979323846;
}

}  // namespace leaf::detail
