#include "quality.h"

#include <leaf/config.h>

#include <cmath>
#include <gtest/gtest.h>
#include <limits>

namespace {

leaf::detail::Photometry photometry(bool contrast, bool lighting) {
    return {0.5, 0.1, contrast, lighting};
}

}  // namespace

TEST(Quality, IssuesUseFixedOrder) {
    const auto quality = leaf::defaultAnalyzerConfig().quality;
    const auto result =
        leaf::detail::assessQuality(photometry(false, false), 0.1, 0.1, 0.1, 0.1, 0.1, quality);
    ASSERT_TRUE(result.hasValue());
    const auto& assessment = *result.value();
    EXPECT_FALSE(assessment.acceptable);
    ASSERT_EQ(assessment.issues.size(), 7u);
    EXPECT_EQ(assessment.issues[0], leaf::QualityIssue::LowLeafConfidence);
    EXPECT_EQ(assessment.issues[1], leaf::QualityIssue::LowCenterVeinConfidence);
    EXPECT_EQ(assessment.issues[2], leaf::QualityIssue::LowSecondaryVeinConfidence);
    EXPECT_EQ(assessment.issues[3], leaf::QualityIssue::LowKeypointConfidence);
    EXPECT_EQ(assessment.issues[4], leaf::QualityIssue::LowMeasurementConfidence);
    EXPECT_EQ(assessment.issues[5], leaf::QualityIssue::MarginalContrast);
    EXPECT_EQ(assessment.issues[6], leaf::QualityIssue::MarginalLighting);
}

TEST(Quality, MinimumConfidenceIsTheMinOfStages) {
    const auto quality = leaf::defaultAnalyzerConfig().quality;
    const auto result = leaf::detail::assessQuality(photometry(true, true), 0.9, 0.8, 0.7, 0.6, 0.55, quality);
    ASSERT_TRUE(result.hasValue());
    const auto& assessment = *result.value();
    EXPECT_DOUBLE_EQ(assessment.overallConfidence, 0.55);
    EXPECT_TRUE(assessment.acceptable);
    EXPECT_TRUE(assessment.issues.empty());
    EXPECT_DOUBLE_EQ(assessment.leafConfidence, 0.9);
    EXPECT_DOUBLE_EQ(assessment.centerVeinConfidence, 0.8);
    EXPECT_DOUBLE_EQ(assessment.secondaryVeinConfidence, 0.7);
    EXPECT_DOUBLE_EQ(assessment.keypointConfidence, 0.6);
    EXPECT_DOUBLE_EQ(assessment.measurementConfidence, 0.55);
    EXPECT_TRUE(assessment.sufficientContrast);
    EXPECT_TRUE(assessment.sufficientLighting);
}

TEST(Quality, RejectsNonfiniteConfidence) {
    const auto quality = leaf::defaultAnalyzerConfig().quality;
    const auto result = leaf::detail::assessQuality(
        photometry(true, true), std::numeric_limits<double>::quiet_NaN(), 1.0, 1.0, 1.0, 1.0, quality);
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error()->code, leaf::ErrorCode::InternalError);
    EXPECT_EQ(result.error()->stage, leaf::Stage::Quality);
}

TEST(Quality, ThresholdEqualityPassesAndJustBelowAddsOneIssue) {
    const auto quality = leaf::defaultAnalyzerConfig().quality;
    const auto atThreshold = leaf::detail::assessQuality(
        photometry(true, true), quality.minimumLeafConfidence, quality.minimumCenterVeinConfidence,
        quality.minimumSecondaryVeinConfidence, quality.minimumKeypointConfidence,
        quality.minimumMeasurementConfidence, quality);
    ASSERT_TRUE(atThreshold.hasValue());
    EXPECT_TRUE(atThreshold.value()->acceptable);
    EXPECT_TRUE(atThreshold.value()->issues.empty());

    const double justBelow = std::nextafter(quality.minimumLeafConfidence, 0.0);
    const auto below = leaf::detail::assessQuality(
        photometry(true, true), justBelow, quality.minimumCenterVeinConfidence,
        quality.minimumSecondaryVeinConfidence, quality.minimumKeypointConfidence,
        quality.minimumMeasurementConfidence, quality);
    ASSERT_TRUE(below.hasValue());
    EXPECT_FALSE(below.value()->acceptable);
    ASSERT_EQ(below.value()->issues.size(), 1u);
    EXPECT_EQ(below.value()->issues[0], leaf::QualityIssue::LowLeafConfidence);
}
