#include <gtest/gtest.h>

#include <leaf/error.h>
#include <leaf/outcome.h>
#include <leaf/types.h>

#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace leaf::detail {
Outcome<Point> pointAtArc(const Path& path, double s) noexcept;
double arcLength(const Path& path) noexcept;
Outcome<Point> unitTangentRegression(const Path& path, double s0, double s1,
                                     double minSpan) noexcept;
Outcome<std::pair<Point, Point>> contourRayHits(const Path& contour, Point origin,
                                                Point unitXAxis) noexcept;
Outcome<double> sameSideContourArc(const Path& contour, Point from, Point to,
                                   const CoordinateSystem& cs, Point baseSeparator,
                                   Point apexSeparator, double equalityTolerance) noexcept;
Outcome<CoordinateSystem> makeCoordinateSystem(const CenterVein& vein) noexcept;
double unorientedAngleDegrees(Point a, Point b) noexcept;
}  // namespace leaf::detail

namespace {

constexpr double kAbsTol = 1e-12;

leaf::ErrorCode errorCode(const leaf::Outcome<double>& outcome) {
  EXPECT_NE(outcome.error(), nullptr);
  return outcome.error()->code;
}

leaf::ErrorCode errorCode(const leaf::Outcome<leaf::Point>& outcome) {
  EXPECT_NE(outcome.error(), nullptr);
  return outcome.error()->code;
}

leaf::ErrorCode errorCode(
    const leaf::Outcome<std::pair<leaf::Point, leaf::Point>>& outcome) {
  EXPECT_NE(outcome.error(), nullptr);
  return outcome.error()->code;
}

void expectPointNear(const leaf::Point& actual, const leaf::Point& expected,
                     double tolerance) {
  EXPECT_NEAR(actual.x, expected.x, tolerance);
  EXPECT_NEAR(actual.y, expected.y, tolerance);
}

#define EXPECT_POINT_EQ(actual, expected) \
  do {                                  \
    expectPointNear(actual, expected, kAbsTol); \
  } while (0)

leaf::CoordinateSystem axisAlignedCs(double scale) {
  return leaf::CoordinateSystem{{0, 0}, {1, 0}, {0, 1}, scale};
}

}  // namespace

TEST(geometry, ArcTransformAndAmbiguousArc) {
  leaf::Path p{{0, 0}, {0, 2}, {0, 10}};
  EXPECT_POINT_EQ(*leaf::detail::pointAtArc(p, 5).value(), (leaf::Point{0, 5}));
  leaf::CenterVein v{p, {0, 0}, {0, 10}, 1};
  auto cs = leaf::detail::makeCoordinateSystem(v);
  ASSERT_TRUE(cs.hasValue());
  auto q = cs.value()->normalizedToImage(cs.value()->imageToNormalized({3.25, 7.5}));
  EXPECT_NEAR(q.x, 3.25, 1e-8);
  EXPECT_NEAR(q.y, 7.5, 1e-8);
  const auto arc = leaf::detail::sameSideContourArc(
      {{-1, 0}, {-1, 1}, {0, 2}, {1, 1}, {1, 0}, {-1, 0}}, {-1, 1}, {-1, 0}, *cs.value(),
      {0, 0}, {0, 10}, 1e-8);
  ASSERT_TRUE(arc.hasValue());
  EXPECT_NEAR(*arc.value(), 1.0, 1e-12);
}

TEST(geometry, ArcLengthAndInterpolation) {
  const leaf::Path path{{0, 0}, {3, 0}, {3, 4}};
  EXPECT_DOUBLE_EQ(leaf::detail::arcLength(path), 7.0);
  EXPECT_POINT_EQ(*leaf::detail::pointAtArc(path, 0).value(), (leaf::Point{0, 0}));
  EXPECT_POINT_EQ(*leaf::detail::pointAtArc(path, 3).value(), (leaf::Point{3, 0}));
  EXPECT_POINT_EQ(*leaf::detail::pointAtArc(path, 5).value(), (leaf::Point{3, 2}));
  EXPECT_POINT_EQ(*leaf::detail::pointAtArc(path, 7).value(), (leaf::Point{3, 4}));
  EXPECT_FALSE(leaf::detail::pointAtArc(path, -0.1).hasValue());
  EXPECT_FALSE(leaf::detail::pointAtArc(path, 7.1).hasValue());
}

TEST(geometry, ArcLengthWithDuplicatePoints) {
  const leaf::Path path{{0, 0}, {0, 0}, {0, 3}};
  EXPECT_DOUBLE_EQ(leaf::detail::arcLength(path), 3.0);
  EXPECT_POINT_EQ(*leaf::detail::pointAtArc(path, 1.5).value(), (leaf::Point{0, 1.5}));
}

TEST(geometry, CoordinateRoundTripWithinTolerance) {
  leaf::CenterVein vein{{{-2, 1}, {0, 4}, {3, 9}}, {-2, 1}, {3, 9}, 1};
  const auto cs = leaf::detail::makeCoordinateSystem(vein);
  ASSERT_TRUE(cs.hasValue());
  const double scale = cs.value()->scale;
  const leaf::Point image{1.25, 6.75};
  const auto roundTrip =
      cs.value()->normalizedToImage(cs.value()->imageToNormalized(image));
  expectPointNear(roundTrip, image, 1e-9 * scale);
}

TEST(geometry, MakeCoordinateSystemRejectsDegenerateVein) {
  leaf::CenterVein vein{{{0, 0}, {0, 0}}, {0, 0}, {0, 0}, 1};
  const auto cs = leaf::detail::makeCoordinateSystem(vein);
  ASSERT_FALSE(cs.hasValue());
  EXPECT_EQ(cs.error()->code, leaf::ErrorCode::InvalidCoordinateSystem);
  EXPECT_EQ(cs.error()->stage, leaf::Stage::CoordinateSystem);
}

TEST(geometry, UnitTangentRegressionNormalAndDegenerate) {
  const leaf::Path path{{0, 0}, {0, 5}, {0, 10}};
  const auto tangent = leaf::detail::unitTangentRegression(path, 2.0, 8.0, 0.5);
  ASSERT_TRUE(tangent.hasValue());
  EXPECT_NEAR(tangent.value()->x, 0.0, 1e-12);
  EXPECT_NEAR(tangent.value()->y, 1.0, 1e-12);

  const auto tooShort = leaf::detail::unitTangentRegression(path, 4.0, 4.2, 0.5);
  EXPECT_FALSE(tooShort.hasValue());
  EXPECT_EQ(errorCode(tooShort), leaf::ErrorCode::InvalidMeasurements);

  const leaf::Path twoPoints{{0, 0}, {1, 0}};
  const auto insufficient = leaf::detail::unitTangentRegression(twoPoints, 0.0, 1.0, 0.1);
  EXPECT_FALSE(insufficient.hasValue());
  EXPECT_EQ(errorCode(insufficient), leaf::ErrorCode::InvalidMeasurements);
}

TEST(geometry, UnitTangentRegressionOrientedByEndpointProgression) {
  const leaf::Path path{{0, 0}, {0, 5}, {0, 10}};
  const auto tangent = leaf::detail::unitTangentRegression(path, 1.0, 9.0, 0.5);
  ASSERT_TRUE(tangent.hasValue());
  EXPECT_NEAR(tangent.value()->x, 0.0, 1e-12);
  EXPECT_GT(tangent.value()->y, 0.0);
}

TEST(geometry, ContourRayHitsNearestSignedDirections) {
  const leaf::Path contour{{-5, 2}, {0, 2}, {5, 2}, {5, -1}, {-5, -1}, {-5, 2}};
  const auto hits = leaf::detail::contourRayHits(contour, {0, 2}, {1, 0});
  ASSERT_TRUE(hits.hasValue());
  EXPECT_POINT_EQ(hits.value()->first, (leaf::Point{-5, 2}));
  EXPECT_POINT_EQ(hits.value()->second, (leaf::Point{5, 2}));
}

TEST(geometry, ContourRayHitsParallelRayFails) {
  const leaf::Path contour{{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}};
  const auto hits = leaf::detail::contourRayHits(contour, {0.5, 2.0}, {1, 0});
  EXPECT_FALSE(hits.hasValue());
  EXPECT_EQ(errorCode(hits), leaf::ErrorCode::InvalidMeasurements);
}

TEST(geometry, ContourRayHitsDeduplicatesVertexIntersection) {
  const leaf::Path contour{{0, 0}, {2, 0}, {2, 2}, {0, 2}, {0, 0}};
  const auto hits = leaf::detail::contourRayHits(contour, {1, 0}, {1, 0});
  ASSERT_TRUE(hits.hasValue());
  EXPECT_POINT_EQ(hits.value()->first, (leaf::Point{0, 0}));
  EXPECT_POINT_EQ(hits.value()->second, (leaf::Point{2, 0}));
}

TEST(geometry, SameSideContourArcChoosesShortestValidPath) {
  const leaf::CoordinateSystem cs = axisAlignedCs(10.0);
  const leaf::Path contour{{-4, 1}, {-4, 0}, {0, 3}, {4, 0}, {4, 1}, {0, 2}};
  const auto arc = leaf::detail::sameSideContourArc(contour, {-4, 1}, {-4, 0}, cs, {0, 0},
                                                    {0, 10}, 1e-9 * cs.scale);
  ASSERT_TRUE(arc.hasValue());
  EXPECT_NEAR(*arc.value(), 1.0, 1e-12);
}

TEST(geometry, SameSideContourArcRejectsOppositeSideTraversal) {
  const leaf::CoordinateSystem cs = axisAlignedCs(10.0);
  const leaf::Path contour{{-2, 0}, {-2, 2}, {0, 4}, {2, 2}, {2, 0}, {-2, 0}};
  const auto arc = leaf::detail::sameSideContourArc(contour, {-2, 2}, {2, 2}, cs, {0, 0},
                                                    {0, 10}, 1e-9 * cs.scale);
  EXPECT_FALSE(arc.hasValue());
  EXPECT_EQ(errorCode(arc), leaf::ErrorCode::InvalidMeasurements);
}

TEST(geometry, SameSideContourArcIgnoresGeometricallyIdenticalDuplicateRoutes) {
  const leaf::CoordinateSystem cs = axisAlignedCs(10.0);
  const leaf::Path contour{{-4, 1}, {-4, 0}, {0, 3}, {4, 0}, {4, 1}, {0, 2}, {-4, 1}};
  const auto arc = leaf::detail::sameSideContourArc(contour, {-4, 1}, {-4, 0}, cs, {0, 0},
                                                    {0, 10}, 1e-9 * cs.scale);
  ASSERT_TRUE(arc.hasValue());
  EXPECT_NEAR(*arc.value(), 1.0, 1e-12);
}

TEST(geometry, SameSideContourArcRejectsGeometricallyDistinctEqualValidArcs) {
  const leaf::CoordinateSystem cs = axisAlignedCs(10.0);
  const leaf::Path contour{{-2, 0}, {-2, 2}, {-3, 3}, {-2, 4}, {-1, 3}, {-2, 2}, {0, 5},
                           {1, 3},  {2, 2},  {2, 0},  {-2, 0}};
  const auto arc = leaf::detail::sameSideContourArc(contour, {-2, 2}, {-2, 4}, cs, {0, 0},
                                                    {0, 10}, 1e-8);
  EXPECT_FALSE(arc.hasValue());
  EXPECT_EQ(errorCode(arc), leaf::ErrorCode::InvalidMeasurements);
}

TEST(geometry, UnitTangentRegressionRejectsSingleSegmentWindow) {
  const leaf::Path path{{0, 0}, {0, 10}, {0, 20}, {0, 30}};
  const auto tangent = leaf::detail::unitTangentRegression(path, 5.5, 6.5, 0.5);
  EXPECT_FALSE(tangent.hasValue());
  EXPECT_EQ(errorCode(tangent), leaf::ErrorCode::InvalidMeasurements);
}

TEST(geometry, UnorientedAngleDegrees) {
  EXPECT_NEAR(leaf::detail::unorientedAngleDegrees({1, 0}, {1, 0}), 0.0, 1e-12);
  EXPECT_NEAR(leaf::detail::unorientedAngleDegrees({1, 0}, {0, 1}), 90.0, 1e-12);
  EXPECT_NEAR(leaf::detail::unorientedAngleDegrees({1, 0}, {-1, 0}), 180.0, 1e-12);
  EXPECT_NEAR(leaf::detail::unorientedAngleDegrees({1, 0}, {-1, 1}), 135.0, 1e-12);
}

struct RoundTripCase {
  std::string name;
  leaf::CenterVein vein;
  leaf::Point image;
};

class CoordinateRoundTripTest : public ::testing::TestWithParam<RoundTripCase> {};

TEST_P(CoordinateRoundTripTest, ImageNormalizedRoundTrip) {
  const auto cs = leaf::detail::makeCoordinateSystem(GetParam().vein);
  ASSERT_TRUE(cs.hasValue());
  const double tolerance = 1e-9 * cs.value()->scale;
  const auto roundTrip =
      cs.value()->normalizedToImage(cs.value()->imageToNormalized(GetParam().image));
  expectPointNear(roundTrip, GetParam().image, tolerance);
}

INSTANTIATE_TEST_SUITE_P(
    geometry, CoordinateRoundTripTest,
    ::testing::Values(
        RoundTripCase{"origin", {{{0, 0}, {0, 10}}, {0, 0}, {0, 10}, 1.0}, {0, 0}},
        RoundTripCase{"positive_quadrant", {{{0, 0}, {0, 10}}, {0, 0}, {0, 10}, 1}, {4, 7}},
        RoundTripCase{"negative_x", {{{0, 0}, {0, 10}}, {0, 0}, {0, 10}, 1}, {-3, 6}}),
    [](const ::testing::TestParamInfo<RoundTripCase>& info) { return info.param.name; });
