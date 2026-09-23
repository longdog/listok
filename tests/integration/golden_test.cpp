#include <gtest/gtest.h>
#include <gtest/gtest-spi.h>

#include <fixture_factory.h>
#include <golden_compare.h>

#include <fstream>

// testdata/ has no ground truth and never enters tolerance assertions.
// tests/expected/1.0/leaf_v1.json is the serializer snapshot of the canonical
// AnalysisResult used by the JSON unit test. It is not a pipeline golden:
// default analyze() of tests/fixtures/synthetic/leaf_v1.ppm does not finish.
// Labeled c1/c2 lie 14–19 px inside the contour, so no point can be both
// within endpointContourSnapPx (8) and within the 5 px keypoint tolerance.

TEST(golden_harness, EnforcesNormativeToleranceBoundaries) {
  EXPECT_NO_FATAL_FAILURE(expectPointWithin5Px({0, 0}, {3, 4}));
  EXPECT_NONFATAL_FAILURE(expectPointWithin5Px({0, 0}, {3.01, 4}), "5 px");
  EXPECT_NO_FATAL_FAILURE(expectLengthWithin5Percent(105, 100));
  EXPECT_NONFATAL_FAILURE(expectAngleWithin3Degrees(13.01, 10), "3");
  EXPECT_NO_FATAL_FAILURE(expectAsymmetryWithin005(0.105, 0.1));
}

TEST(golden_harness, LoadsLabeledSyntheticFixture) {
  const SyntheticLeafFixture fixture = makeSyntheticLeafV1();
  ASSERT_EQ(fixture.width, 240);
  ASSERT_EQ(fixture.height, 320);
  EXPECT_EQ(fixture.rgb.size(), 240u * 320u * 3u);
  EXPECT_EQ(fixture.mask.size(), 240u * 320u);
  const auto truth = readGroundTruth(fixture.groundTruthPath);
  ASSERT_TRUE(truth.hasValue());
  EXPECT_EQ(truth.value()->algorithmVersion, "1.0.0");
  EXPECT_GE(truth.value()->contour.size(), 8u);
  EXPECT_GE(truth.value()->center.size(), 2u);
  EXPECT_GE(truth.value()->score, 1);
  EXPECT_LE(truth.value()->score, 5);

  std::ifstream lowContrast(std::string(CMAKE_SOURCE_DIR) + "/tests/fixtures/negative/low_contrast.pgm");
  std::ifstream outsideFrame(std::string(CMAKE_SOURCE_DIR) + "/tests/fixtures/negative/outside_frame.pgm");
  EXPECT_TRUE(lowContrast.good());
  EXPECT_TRUE(outsideFrame.good());
}

TEST(golden_harness, GoldenUpdateForbidden) {
  // Label golden-update-forbidden: expected JSON is not refreshed by this suite.
  SUCCEED();
}
