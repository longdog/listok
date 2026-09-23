#include <gtest/gtest.h>

#include <leaf/error.h>
#include <leaf/outcome.h>
#include <leaf/types.h>

#include <extractor.h>
#include <fixture_factory.h>

#include <cmath>
#include <limits>

namespace {

template <typename T>
void expectMeasurementFailure(const leaf::Outcome<T>& outcome,
                              leaf::ErrorCode code = leaf::ErrorCode::InvalidMeasurements) {
  ASSERT_FALSE(outcome.hasValue());
  ASSERT_NE(outcome.error(), nullptr);
  EXPECT_EQ(outcome.error()->code, code);
  EXPECT_EQ(outcome.error()->stage, leaf::Stage::Measurements);
}

leaf::CoordinateSystem axisAlignedCs(double scale) {
  return leaf::CoordinateSystem{{0, 0}, {1, 0}, {0, 1}, scale};
}

leaf::Outcome<leaf::LeafMeasurements> extract(const MeasurementDomain& domain) {
  return leaf::detail::extractMeasurements(domain.contour, domain.center, domain.veins,
                                           domain.keypoints, domain.cs);
}

leaf::Path leafContour(double leftX, double rightX, const MeasurementDomain& domain) {
  const leaf::Point leftA = domain.veins.leftFirst.endpoint;
  const leaf::Point leftB = domain.veins.leftSecond.endpoint;
  const leaf::Point rightA = domain.veins.rightFirst.endpoint;
  const leaf::Point rightB = domain.veins.rightSecond.endpoint;
  return {{leftX, 5}, {leftX, 10}, {0, 11}, {rightX, 10}, rightA, rightB, {rightX, 5},
          {rightX, 0}, {0, -1},    {leftX, 0}, leftA,     leftB,  {leftX, 5}};
}

}  // namespace

TEST(measurement, ComputesNormativeFiveFeatures) {
  const auto domain = canonicalMeasurementDomain();
  const auto measurements = extract(domain);
  ASSERT_TRUE(measurements.hasValue());
  EXPECT_DOUBLE_EQ(measurements.value()->left.m1, 0.4);
  EXPECT_DOUBLE_EQ(measurements.value()->right.m1, 0.5);
  EXPECT_DOUBLE_EQ(measurements.value()->left.m2, 0.3);
  EXPECT_DOUBLE_EQ(measurements.value()->right.m2, 0.4);
  EXPECT_DOUBLE_EQ(measurements.value()->left.m3, 0.2);
  EXPECT_DOUBLE_EQ(measurements.value()->right.m3, 0.25);
  EXPECT_DOUBLE_EQ(measurements.value()->left.m4, 0.25);
  EXPECT_DOUBLE_EQ(measurements.value()->right.m4, 0.3);
  EXPECT_NEAR(measurements.value()->left.m5, 45.0, 1e-12);
  EXPECT_NEAR(measurements.value()->right.m5, 60.0, 1e-12);
}

TEST(measurement, MidpointInterpolatesInsideCenterSegment) {
  auto domain = canonicalMeasurementDomain();
  domain.center.path = {{0, 0}, {0, 2}, {0, 4}, {0, 4.5}, {0, 6}, {0, 10}};
  domain.cs = axisAlignedCs(10.0);
  const auto measurements = extract(domain);
  ASSERT_TRUE(measurements.hasValue()) << (measurements.error() ? measurements.error()->message : "");
  EXPECT_DOUBLE_EQ(measurements.value()->left.m1, 0.4);
  EXPECT_DOUBLE_EQ(measurements.value()->right.m1, 0.5);
}

TEST(measurement, LeftGreaterThanRight) {
  auto domain = canonicalMeasurementDomain();
  domain.contour.path = leafContour(-6.0, 5.0, domain);
  const auto measurements = extract(domain);
  ASSERT_TRUE(measurements.hasValue());
  EXPECT_GT(measurements.value()->left.m1, measurements.value()->right.m1);
}

TEST(measurement, LeftLessThanRight) {
  const auto domain = canonicalMeasurementDomain();
  const auto measurements = extract(domain);
  ASSERT_TRUE(measurements.hasValue());
  EXPECT_LT(measurements.value()->left.m1, measurements.value()->right.m1);
  EXPECT_LT(measurements.value()->left.m2, measurements.value()->right.m2);
  EXPECT_LT(measurements.value()->left.m3, measurements.value()->right.m3);
  EXPECT_LT(measurements.value()->left.m4, measurements.value()->right.m4);
  EXPECT_LT(measurements.value()->left.m5, measurements.value()->right.m5);
}

TEST(measurement, EqualLeftRightM1) {
  auto domain = canonicalMeasurementDomain();
  domain.contour.path = leafContour(-4.0, 4.0, domain);
  const auto measurements = extract(domain);
  ASSERT_TRUE(measurements.hasValue());
  EXPECT_DOUBLE_EQ(measurements.value()->left.m1, measurements.value()->right.m1);
}

TEST(measurement, RejectsDegenerateCenterVein) {
  auto domain = canonicalMeasurementDomain();
  domain.center.path = {{0, 0}, {0, 0}};
  domain.center.base = {0, 0};
  domain.center.apex = {0, 0};
  expectMeasurementFailure(extract(domain));
}

TEST(measurement, RejectsInvalidCoordinateSystem) {
  auto domain = canonicalMeasurementDomain();
  domain.cs.scale = 0.0;
  expectMeasurementFailure(extract(domain), leaf::ErrorCode::InvalidCoordinateSystem);

  domain.cs.scale = std::numeric_limits<double>::quiet_NaN();
  expectMeasurementFailure(extract(domain), leaf::ErrorCode::InvalidCoordinateSystem);

  domain.cs = axisAlignedCs(-1.0);
  expectMeasurementFailure(extract(domain), leaf::ErrorCode::InvalidCoordinateSystem);
}

TEST(measurement, DuplicateContourVerticesDoNotCreateFalseM4Ambiguity) {
  auto domain = canonicalMeasurementDomain();
  const leaf::Point leftA = domain.veins.leftFirst.endpoint;
  const leaf::Point leftB = domain.veins.leftSecond.endpoint;
  const leaf::Point rightA = domain.veins.rightFirst.endpoint;
  const leaf::Point rightB = domain.veins.rightSecond.endpoint;
  domain.contour.path = {{-4, 5}, {-4, 10}, {0, 11}, {5, 10}, rightA, rightB, {5, 5},
                         {5, 0},  {0, -1},  {-4, 0}, leftA,  leftA,  leftB,  {-4, 5}};
  const auto measurements = extract(domain);
  ASSERT_TRUE(measurements.hasValue()) << (measurements.error() ? measurements.error()->message : "");
  EXPECT_DOUBLE_EQ(measurements.value()->left.m4, 0.25);
}

TEST(measurement, RejectsAmbiguousM4Arc) {
  auto domain = canonicalMeasurementDomain();
  domain.veins.leftFirst.endpoint = {-2, 2};
  domain.veins.leftSecond.endpoint = {-2, 4};
  const leaf::Point rightA = domain.veins.rightFirst.endpoint;
  const leaf::Point rightB = domain.veins.rightSecond.endpoint;
  domain.contour.path = {{-4, 5}, {-4, 10}, {0, 11}, {5, 10}, rightA, rightB, {5, 5},
                         {5, 0},  {0, -1},  {-4, 0}, {-2, 2}, {-3, 3}, {-2, 4}, {-1, 3},
                         {-2, 2}, {-4, 5}};
  expectMeasurementFailure(extract(domain));
}

TEST(measurement, RejectsInsufficientTangentWindowForM5) {
  auto domain = canonicalMeasurementDomain();
  domain.veins.leftSecond.path = {{0, 4}, {0, 4.05}};
  expectMeasurementFailure(extract(domain));
}

TEST(measurement, AcceptsExactBoundaryTangentWindowForM5) {
  auto domain = canonicalMeasurementDomain();
  const double halfWindow = 0.5 * 0.02 * domain.cs.scale;
  domain.veins.leftSecond.path = {{0, 4}, {-halfWindow / 2.0, 4.0}, {-halfWindow, 4.0}};
  domain.veins.leftSecond.endpoint = {-halfWindow, 4.0};
  const leaf::Point leftA = domain.veins.leftFirst.endpoint;
  const leaf::Point rightA = domain.veins.rightFirst.endpoint;
  const leaf::Point rightB = domain.veins.rightSecond.endpoint;
  domain.contour.path = {{-4, 5}, {-4, 10}, {0, 11}, {5, 10}, rightA, rightB, {5, 5},
                         {5, 0},  {0, -1},  {-4, 0}, leftA,  {-halfWindow, 4.0}, {-4, 5}};
  const auto measurements = extract(domain);
  ASSERT_TRUE(measurements.hasValue()) << (measurements.error() ? measurements.error()->message : "");
  EXPECT_NEAR(measurements.value()->left.m5, 90.0, 1e-6);
}

TEST(measurement, RoundTripNormalizedM1Values) {
  const auto domain = canonicalMeasurementDomain();
  const auto measurements = extract(domain);
  ASSERT_TRUE(measurements.hasValue());
  const double scale = domain.cs.scale;
  const leaf::Point f = domain.keypoints.f.image;
  const leaf::Point leftHit = domain.keypoints.g1.image;
  const leaf::Point rightHit = domain.keypoints.g2.image;
  EXPECT_DOUBLE_EQ(measurements.value()->left.m1,
                   std::hypot(leftHit.x - f.x, leftHit.y - f.y) / scale);
  EXPECT_DOUBLE_EQ(measurements.value()->right.m1,
                   std::hypot(rightHit.x - f.x, rightHit.y - f.y) / scale);
}

TEST(measurement, RejectsOppositeSideContourArcForM4) {
  auto domain = canonicalMeasurementDomain();
  domain.veins.leftSecond.endpoint = domain.veins.rightSecond.endpoint;
  expectMeasurementFailure(extract(domain));
}
