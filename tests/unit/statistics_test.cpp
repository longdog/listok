#include <gtest/gtest.h>

#include <leaf/config.h>
#include <leaf/error.h>
#include <leaf/outcome.h>
#include <leaf/result.h>
#include <leaf/types.h>

#include <asymmetry.h>
#include <batch.h>
#include <score.h>

#include <cmath>
#include <initializer_list>
#include <limits>
#include <vector>

namespace {

const std::array<leaf::ScoreRange, 5>& defaultRanges() {
  static const auto ranges = leaf::defaultScoreRanges();
  return ranges;
}

leaf::Versions versions() { return leaf::defaultVersions(); }

leaf::BatchItem accepted(double asymmetry) {
  leaf::AnalysisResult result;
  result.asymmetry.value = asymmetry;
  result.quality.acceptable = true;
  return leaf::BatchItem{leaf::Outcome<leaf::AnalysisResult>::success(std::move(result))};
}

leaf::BatchItem rejected(double asymmetry) {
  leaf::AnalysisResult result;
  result.asymmetry.value = asymmetry;
  result.quality.acceptable = false;
  return leaf::BatchItem{leaf::Outcome<leaf::AnalysisResult>::success(std::move(result))};
}

leaf::BatchItem failed() {
  return leaf::BatchItem{leaf::Outcome<leaf::AnalysisResult>::failure(
      {leaf::ErrorCode::LeafNotFound, leaf::Stage::LeafContour, "missing leaf"})};
}

std::vector<leaf::BatchItem> items(std::initializer_list<leaf::BatchItem> values) {
  return std::vector<leaf::BatchItem>(values);
}

leaf::LeafMeasurements measurementsWith(double leftM1, double rightM1, double leftM3,
                                        double rightM3) {
  leaf::LeafMeasurements measurements{{leftM1, 3, 4, 5, 30}, {rightM1, 3, 8, 5, 60}, 1};
  measurements.left.m3 = leftM3;
  measurements.right.m3 = rightM3;
  return measurements;
}

}  // namespace

TEST(statistics, SignedFormulaScoreAndBatchAreExact) {
  const leaf::LeafMeasurements measurements{{2, 3, 4, 5, 30}, {1, 3, 8, 5, 60}, 1};
  const auto asymmetry = leaf::detail::calculateAsymmetry(measurements, 1e-9);
  ASSERT_TRUE(asymmetry.hasValue());
  EXPECT_DOUBLE_EQ(asymmetry.value()->features.m1, 1.0 / 3);
  EXPECT_DOUBLE_EQ(asymmetry.value()->features.m2, 0.0);
  EXPECT_DOUBLE_EQ(asymmetry.value()->features.m3, -1.0 / 3);
  EXPECT_DOUBLE_EQ(asymmetry.value()->features.m4, 0.0);
  EXPECT_DOUBLE_EQ(asymmetry.value()->features.m5, -1.0 / 3);
  EXPECT_DOUBLE_EQ(asymmetry.value()->value, (1.0 / 3 + 0 - 1.0 / 3 + 0 - 1.0 / 3) / 5);

  EXPECT_EQ(*leaf::detail::calculateScore(0.040, defaultRanges()).value(), 2);
  EXPECT_EQ(*leaf::detail::calculateScore(-0.055, defaultRanges()).value(), 5);

  const auto batch = leaf::detail::aggregateBatch(
      items({accepted(0.02), failed(), rejected(0.5), accepted(-0.04)}), versions(),
      defaultRanges());
  EXPECT_EQ(batch.items.size(), 4u);
  ASSERT_TRUE(batch.items[0].outcome.hasValue());
  ASSERT_FALSE(batch.items[1].outcome.hasValue());
  EXPECT_EQ(batch.items[1].outcome.error()->code, leaf::ErrorCode::LeafNotFound);
  ASSERT_TRUE(batch.items[2].outcome.hasValue());
  EXPECT_FALSE(batch.items[2].outcome.value()->quality.acceptable);
  EXPECT_EQ(batch.total, 4u);
  EXPECT_EQ(batch.successful, 3u);
  EXPECT_EQ(batch.acceptable, 2u);
  ASSERT_TRUE(batch.meanAsymmetry.has_value());
  EXPECT_DOUBLE_EQ(*batch.meanAsymmetry, -0.01);
  EXPECT_TRUE(batch.valid);
  EXPECT_EQ(batch.score, 1);
  EXPECT_EQ(batch.versions.resultSchemaVersion, "1.0");
}

TEST(statistics, ScoreBoundariesUseHalfOpenRanges) {
  const double boundaries[] = {0.0, 0.040, 0.045, 0.050, 0.055};
  const int expected[] = {1, 2, 3, 4, 5};
  for (int index = 0; index < 5; ++index) {
    EXPECT_EQ(*leaf::detail::calculateScore(boundaries[index], defaultRanges()).value(),
              expected[index])
        << boundaries[index];
    if (index > 0) {
      const double justBelow = std::nextafter(boundaries[index], 0.0);
      EXPECT_EQ(*leaf::detail::calculateScore(justBelow, defaultRanges()).value(), expected[index - 1])
          << justBelow;
    }
  }
  const double aboveLast = std::nextafter(0.055, 1.0);
  EXPECT_EQ(*leaf::detail::calculateScore(aboveLast, defaultRanges()).value(), 5);
  EXPECT_EQ(*leaf::detail::calculateScore(-0.0, defaultRanges()).value(), 1);
}

TEST(statistics, DenominatorBoundaryIsStrict) {
  const double minDenominator = 1e-9;
  auto below = measurementsWith(std::nextafter(minDenominator, 0.0), 0.0, 1.0, 1.0);
  const auto belowResult = leaf::detail::calculateAsymmetry(below, minDenominator);
  ASSERT_FALSE(belowResult.hasValue());
  EXPECT_EQ(belowResult.error()->code, leaf::ErrorCode::ZeroAsymmetryDenominator);
  EXPECT_EQ(belowResult.error()->stage, leaf::Stage::Statistics);

  auto equal = measurementsWith(minDenominator, 0.0, 1.0, 1.0);
  const auto equalResult = leaf::detail::calculateAsymmetry(equal, minDenominator);
  ASSERT_TRUE(equalResult.hasValue());
  EXPECT_DOUBLE_EQ(equalResult.value()->features.m1, 1.0);

  auto above = measurementsWith(std::nextafter(minDenominator, 1.0), 0.0, 1.0, 1.0);
  ASSERT_TRUE(leaf::detail::calculateAsymmetry(above, minDenominator).hasValue());
}

TEST(statistics, RejectsNonfiniteAndNegativeMeasurements) {
  auto nanMeasurements = measurementsWith(1.0, 1.0, 1.0, 1.0);
  nanMeasurements.left.m2 = std::numeric_limits<double>::quiet_NaN();
  const auto nanResult = leaf::detail::calculateAsymmetry(nanMeasurements, 1e-9);
  ASSERT_FALSE(nanResult.hasValue());
  EXPECT_EQ(nanResult.error()->code, leaf::ErrorCode::InvalidMeasurements);
  EXPECT_EQ(nanResult.error()->stage, leaf::Stage::Statistics);

  auto negative = measurementsWith(-0.1, 1.0, 1.0, 1.0);
  EXPECT_EQ(leaf::detail::calculateAsymmetry(negative, 1e-9).error()->code,
            leaf::ErrorCode::InvalidMeasurements);

  auto angle = measurementsWith(1.0, 1.0, 1.0, 1.0);
  angle.left.m5 = 181.0;
  EXPECT_EQ(leaf::detail::calculateAsymmetry(angle, 1e-9).error()->code,
            leaf::ErrorCode::InvalidMeasurements);

  const auto score = leaf::detail::calculateScore(std::numeric_limits<double>::infinity(),
                                                   defaultRanges());
  ASSERT_FALSE(score.hasValue());
  EXPECT_EQ(score.error()->code, leaf::ErrorCode::InvalidMeasurements);
  EXPECT_EQ(score.error()->stage, leaf::Stage::Score);
}

TEST(statistics, MalformedScoreRangesFail) {
  auto ranges = defaultRanges();
  ranges[0].min = 0.01;
  const auto score = leaf::detail::calculateScore(0.0, ranges);
  ASSERT_FALSE(score.hasValue());
  EXPECT_EQ(score.error()->code, leaf::ErrorCode::InvalidScoreRanges);
  EXPECT_EQ(score.error()->stage, leaf::Stage::Score);

  ranges = defaultRanges();
  ranges[1].min = 0.041;
  EXPECT_EQ(leaf::detail::calculateScore(0.02, ranges).error()->code,
            leaf::ErrorCode::InvalidScoreRanges);

  ranges = defaultRanges();
  ranges[4].max = 1.0;
  EXPECT_EQ(leaf::detail::calculateScore(0.2, ranges).error()->code,
            leaf::ErrorCode::InvalidScoreRanges);
}

TEST(statistics, EmptyFailedAndRejectedBatchesAreInvalid) {
  const auto empty =
      leaf::detail::aggregateBatch({}, versions(), defaultRanges());
  EXPECT_EQ(empty.total, 0u);
  EXPECT_EQ(empty.successful, 0u);
  EXPECT_EQ(empty.acceptable, 0u);
  EXPECT_FALSE(empty.meanAsymmetry.has_value());
  EXPECT_FALSE(empty.valid);
  EXPECT_EQ(empty.score, 0);

  const auto allFailed =
      leaf::detail::aggregateBatch(items({failed(), failed()}), versions(), defaultRanges());
  EXPECT_EQ(allFailed.total, 2u);
  EXPECT_EQ(allFailed.successful, 0u);
  EXPECT_FALSE(allFailed.valid);
  EXPECT_FALSE(allFailed.meanAsymmetry.has_value());
  EXPECT_EQ(allFailed.score, 0);
  EXPECT_EQ(allFailed.items.size(), 2u);

  const auto allRejected = leaf::detail::aggregateBatch(
      items({rejected(0.2), rejected(-0.3)}), versions(), defaultRanges());
  EXPECT_EQ(allRejected.successful, 2u);
  EXPECT_EQ(allRejected.acceptable, 0u);
  EXPECT_FALSE(allRejected.valid);
  EXPECT_FALSE(allRejected.meanAsymmetry.has_value());
  EXPECT_EQ(allRejected.score, 0);
  EXPECT_EQ(allRejected.items[0].outcome.value()->asymmetry.value, 0.2);
  EXPECT_EQ(allRejected.items[1].outcome.value()->asymmetry.value, -0.3);
}

TEST(statistics, EqualLeftAndRightAreZeroAsymmetry) {
  const leaf::LeafMeasurements measurements{{2, 3, 4, 5, 40}, {2, 3, 4, 5, 40}, 1};
  const auto asymmetry = leaf::detail::calculateAsymmetry(measurements, 1e-9);
  ASSERT_TRUE(asymmetry.hasValue());
  EXPECT_DOUBLE_EQ(asymmetry.value()->features.m1, 0.0);
  EXPECT_DOUBLE_EQ(asymmetry.value()->value, 0.0);
  EXPECT_EQ(*leaf::detail::calculateScore(0.0, defaultRanges()).value(), 1);
}
