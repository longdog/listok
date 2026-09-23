#include <gtest/gtest.h>

#include <leaf/config.h>
#include <leaf/error.h>

#include <opencv_leaf_detector.h>
#include <preprocessor.h>

#include <opencv2/imgproc.hpp>

#include <cmath>

namespace {

leaf::DetectionConfig testConfig() {
  leaf::DetectionConfig config = leaf::defaultAnalyzerConfig().detection;
  config.minLeafAreaRatio = 0.01;
  config.maxLeafAreaRatio = 0.95;
  config.frameMarginPx = 1;
  config.minContourSolidity = 0.5;
  config.minimumStageConfidence = 0.0;
  config.ambiguityScoreDelta = 0.0;
  config.maxContourCandidates = 8;
  return config;
}

leaf::detail::PreprocessResult imageOf(cv::Mat binary, double sourcePerWorkingX = 1,
                                       double sourcePerWorkingY = 1) {
  leaf::detail::PreprocessResult result;
  result.binary = std::move(binary);
  result.gray = result.binary.clone();
  result.transform.workingWidth = result.binary.cols;
  result.transform.workingHeight = result.binary.rows;
  result.transform.sourcePerWorkingX = sourcePerWorkingX;
  result.transform.sourcePerWorkingY = sourcePerWorkingY;
  result.transform.sourceWidth = static_cast<std::int32_t>(
      std::lround(result.binary.cols * sourcePerWorkingX));
  result.transform.sourceHeight = static_cast<std::int32_t>(
      std::lround(result.binary.rows * sourcePerWorkingY));
  result.photometry = {128, 20, true, true};
  return result;
}

cv::Mat canvas(int width, int height) { return cv::Mat(height, width, CV_8UC1, cv::Scalar(0)); }

void fillRect(cv::Mat& image, int x, int y, int width, int height) {
  cv::rectangle(image, cv::Rect(x, y, width, height), cv::Scalar(255), cv::FILLED);
}

void expectCode(const leaf::Outcome<leaf::LeafContour>& outcome, leaf::ErrorCode code) {
  ASSERT_FALSE(outcome.hasValue());
  ASSERT_NE(outcome.error(), nullptr);
  EXPECT_EQ(outcome.error()->code, code);
  EXPECT_EQ(outcome.error()->stage, leaf::Stage::LeafContour);
}

}  // namespace

TEST(contour, UsesStableRankingAndExactFailures) {
  cv::Mat two = canvas(200, 200);
  fillRect(two, 10, 20, 30, 30);
  fillRect(two, 80, 20, 30, 30);
  auto config = testConfig();
  config.ambiguityScoreDelta = 0;
  const auto ranked = leaf::detail::detectLeafContour(imageOf(two), config);
  ASSERT_TRUE(ranked.hasValue());
  EXPECT_DOUBLE_EQ(ranked.value()->boundingBox.x, 10);

  expectCode(leaf::detail::detectLeafContour(imageOf(canvas(80, 80)), testConfig()),
             leaf::ErrorCode::LeafNotFound);

  cv::Mat tiny = canvas(100, 100);
  fillRect(tiny, 40, 40, 2, 2);
  auto tinyConfig = leaf::defaultAnalyzerConfig().detection;
  tinyConfig.frameMarginPx = 0;
  expectCode(leaf::detail::detectLeafContour(imageOf(tiny), tinyConfig),
             leaf::ErrorCode::LeafTooSmall);

  cv::Mat touching = canvas(100, 100);
  fillRect(touching, 0, 10, 70, 70);
  auto frameConfig = testConfig();
  frameConfig.frameMarginPx = 2;
  expectCode(leaf::detail::detectLeafContour(imageOf(touching), frameConfig),
             leaf::ErrorCode::LeafOutsideFrame);

  cv::Mat near = canvas(200, 200);
  fillRect(near, 10, 30, 40, 40);
  fillRect(near, 80, 30, 42, 40);
  auto ambiguous = testConfig();
  ambiguous.ambiguityScoreDelta = 0.03;
  expectCode(leaf::detail::detectLeafContour(imageOf(near), ambiguous),
             leaf::ErrorCode::AmbiguousLeafContour);
}

TEST(contour, CapsCandidatesRestoresSourceAndKeepsExactDelta) {
  cv::Mat many = canvas(220, 220);
  fillRect(many, 10, 10, 20, 20);
  fillRect(many, 80, 10, 20, 20);
  fillRect(many, 10, 80, 50, 50);
  auto capped = testConfig();
  capped.minLeafAreaRatio = 0.001;
  capped.maxContourCandidates = 1;
  capped.ambiguityScoreDelta = 1;
  const auto onlyFirst = leaf::detail::detectLeafContour(imageOf(many), capped);
  ASSERT_TRUE(onlyFirst.hasValue());
  EXPECT_DOUBLE_EQ(onlyFirst.value()->boundingBox.x, 10);
  EXPECT_DOUBLE_EQ(onlyFirst.value()->boundingBox.y, 10);

  cv::Mat scaled = canvas(100, 50);
  fillRect(scaled, 10, 10, 20, 10);
  const auto restored = leaf::detail::detectLeafContour(imageOf(scaled, 2, 3), testConfig());
  ASSERT_TRUE(restored.hasValue());
  EXPECT_DOUBLE_EQ(restored.value()->boundingBox.x, 20);
  EXPECT_DOUBLE_EQ(restored.value()->boundingBox.y, 30);
  EXPECT_NEAR(restored.value()->boundingBox.width, 40, 2);
  EXPECT_NEAR(restored.value()->boundingBox.height, 30, 3);

  cv::Mat equal = canvas(200, 200);
  fillRect(equal, 10, 20, 30, 30);
  fillRect(equal, 80, 20, 30, 30);
  auto exact = testConfig();
  exact.ambiguityScoreDelta = 0;
  ASSERT_TRUE(leaf::detail::detectLeafContour(imageOf(equal), exact).hasValue());
  exact.ambiguityScoreDelta = std::nextafter(0.0, 1.0);
  expectCode(leaf::detail::detectLeafContour(imageOf(equal), exact),
             leaf::ErrorCode::AmbiguousLeafContour);
}

TEST(contour, SolidityThresholdTieBreakAndTooSmallPrecedence) {
  cv::Mat shape = canvas(160, 160);
  fillRect(shape, 40, 40, 40, 40);
  fillRect(shape, 40, 40, 20, 20);
  cv::rectangle(shape, cv::Rect(40, 40, 20, 20), cv::Scalar(0), cv::FILLED);
  auto strict = testConfig();
  strict.minContourSolidity = 0.95;
  strict.minLeafAreaRatio = 0.001;
  expectCode(leaf::detail::detectLeafContour(imageOf(shape), strict),
             leaf::ErrorCode::LeafNotFound);
  strict.minContourSolidity = 0.5;
  ASSERT_TRUE(leaf::detail::detectLeafContour(imageOf(shape), strict).hasValue());

  cv::Mat tie = canvas(200, 200);
  fillRect(tie, 20, 40, 30, 30);
  fillRect(tie, 90, 40, 30, 30);
  const auto chosen = leaf::detail::detectLeafContour(imageOf(tie), testConfig());
  ASSERT_TRUE(chosen.hasValue());
  EXPECT_DOUBLE_EQ(chosen.value()->boundingBox.x, 20);

  cv::Mat both = canvas(100, 100);
  fillRect(both, 0, 0, 2, 2);
  auto precedence = leaf::defaultAnalyzerConfig().detection;
  precedence.frameMarginPx = 2;
  expectCode(leaf::detail::detectLeafContour(imageOf(both), precedence),
             leaf::ErrorCode::LeafTooSmall);

  cv::Mat wrong(20, 20, CV_32FC1, cv::Scalar(0));
  expectCode(leaf::detail::detectLeafContour(imageOf(wrong), testConfig()),
             leaf::ErrorCode::LeafNotFound);
}
