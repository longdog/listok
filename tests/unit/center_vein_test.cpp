#include <gtest/gtest.h>

#include <leaf/config.h>
#include <leaf/error.h>

#include <center_vein_detector.h>
#include <preprocessor.h>

#include <opencv2/imgproc.hpp>

#include <cmath>

namespace {

leaf::DetectionConfig veinConfig() {
  leaf::DetectionConfig config = leaf::defaultAnalyzerConfig().detection;
  config.minCenterVeinLengthRatio = 0.55;
  config.maxCenterVeinGapPx = 12;
  config.veinThresholdBlockSize = 31;
  config.veinThresholdC = 2;
  config.minimumStageConfidence = 0.2;
  config.ambiguityScoreDelta = 0.03;
  config.frameMarginPx = 0;
  return config;
}

leaf::LeafContour polygonContour(std::initializer_list<cv::Point> points) {
  leaf::LeafContour contour;
  for (const cv::Point& point : points) {
    contour.path.push_back({static_cast<double>(point.x), static_cast<double>(point.y)});
  }
  if (!contour.path.empty()) {
    contour.path.push_back(contour.path.front());
  }
  contour.confidence = 1;
  return contour;
}

leaf::detail::PreprocessResult grayImage(cv::Mat gray) {
  leaf::detail::PreprocessResult result;
  result.gray = std::move(gray);
  result.binary = result.gray.clone();
  result.transform.workingWidth = result.gray.cols;
  result.transform.workingHeight = result.gray.rows;
  result.transform.sourceWidth = result.gray.cols;
  result.transform.sourceHeight = result.gray.rows;
  result.transform.sourcePerWorkingX = 1;
  result.transform.sourcePerWorkingY = 1;
  result.photometry = {128, 30, true, true};
  return result;
}

void expectCode(const leaf::Outcome<leaf::CenterVein>& outcome, leaf::ErrorCode code) {
  ASSERT_FALSE(outcome.hasValue()) << (outcome.hasValue() ? "" : outcome.error()->message);
  ASSERT_NE(outcome.error(), nullptr);
  EXPECT_EQ(outcome.error()->code, code);
  EXPECT_EQ(outcome.error()->stage, leaf::Stage::CenterVein);
}

double pathLength(const leaf::Path& path) {
  double total = 0;
  for (std::size_t i = 1; i < path.size(); ++i) {
    total += std::hypot(path[i].x - path[i - 1].x, path[i].y - path[i - 1].y);
  }
  return total;
}

}  // namespace

TEST(center_vein, SelectsContinuousBaseToApexPath) {
  cv::Mat gray(220, 220, CV_8UC1, cv::Scalar(255));
  const std::vector<cv::Point> trapezoid = {{40, 4}, {160, 4}, {190, 210}, {30, 210}};
  cv::fillPoly(gray, std::vector<std::vector<cv::Point>>{trapezoid}, cv::Scalar(160));
  cv::line(gray, {100, 10}, {100, 190}, cv::Scalar(20), 1, cv::LINE_8);
  const auto contour = polygonContour({{40, 4}, {160, 4}, {190, 210}, {30, 210}});
  const auto detected = leaf::detail::detectCenterVein(grayImage(gray), contour, veinConfig());
  ASSERT_TRUE(detected.hasValue()) << detected.error()->message;
  EXPECT_NEAR(detected.value()->base.x, 100, 5);
  EXPECT_NEAR(detected.value()->base.y, 190, 5);
  EXPECT_NEAR(detected.value()->apex.x, 100, 5);
  EXPECT_NEAR(detected.value()->apex.y, 10, 5);
  EXPECT_GE(pathLength(detected.value()->path) / 180.0, 0.55);

  cv::Mat blank(220, 220, CV_8UC1, cv::Scalar(255));
  cv::fillPoly(blank, std::vector<std::vector<cv::Point>>{std::vector<cv::Point>{{40, 4}, {160, 4}, {190, 210}, {30, 210}}},
               cv::Scalar(160));
  expectCode(leaf::detail::detectCenterVein(grayImage(blank), contour, veinConfig()),
             leaf::ErrorCode::CentralVeinNotFound);

  cv::Mat rectangle(220, 220, CV_8UC1, cv::Scalar(255));
  cv::rectangle(rectangle, cv::Rect(40, 20, 140, 180), cv::Scalar(160), cv::FILLED);
  cv::line(rectangle, {110, 30}, {110, 190}, cv::Scalar(25), 1, cv::LINE_8);
  const auto box = polygonContour({{40, 20}, {180, 20}, {180, 200}, {40, 200}});
  expectCode(leaf::detail::detectCenterVein(grayImage(rectangle), box, veinConfig()),
             leaf::ErrorCode::AmbiguousCentralVein);
}

TEST(center_vein, BridgesExactGapAndRejectsLowConfidence) {
  const std::vector<cv::Point> trapezoid = {{90, 8}, {110, 8}, {170, 205}, {50, 205}};
  const auto contour = polygonContour({{90, 8}, {110, 8}, {170, 205}, {50, 205}});
  auto makeGap = [&](int gap) {
    cv::Mat gray(220, 220, CV_8UC1, cv::Scalar(255));
    cv::fillPoly(gray, std::vector<std::vector<cv::Point>>{trapezoid}, cv::Scalar(160));
    cv::line(gray, {100, 10}, {100, 90}, cv::Scalar(25), 1, cv::LINE_8);
    cv::line(gray, {100, 91 + gap}, {100, 190}, cv::Scalar(25), 1, cv::LINE_8);
    return gray;
  };
  auto bridged = veinConfig();
  bridged.maxCenterVeinGapPx = 12;
  const auto closed = leaf::detail::detectCenterVein(grayImage(makeGap(12)), contour, bridged);
  ASSERT_TRUE(closed.hasValue()) << closed.error()->message;
  EXPECT_GE(pathLength(closed.value()->path) / 180.0, 0.55);

  const auto open = leaf::detail::detectCenterVein(grayImage(makeGap(13)), contour, bridged);
  EXPECT_FALSE(open.hasValue());

  cv::Mat gray(220, 220, CV_8UC1, cv::Scalar(255));
  cv::fillPoly(gray, std::vector<std::vector<cv::Point>>{trapezoid}, cv::Scalar(160));
  cv::line(gray, {100, 10}, {100, 190}, cv::Scalar(25), 1, cv::LINE_8);
  const auto measured = leaf::detail::detectCenterVein(grayImage(gray), contour, veinConfig());
  ASSERT_TRUE(measured.hasValue()) << measured.error()->message;
  auto atThreshold = veinConfig();
  atThreshold.minimumStageConfidence = measured.value()->confidence;
  ASSERT_TRUE(leaf::detail::detectCenterVein(grayImage(gray), contour, atThreshold).hasValue());
  auto above = atThreshold;
  above.minimumStageConfidence = std::nextafter(measured.value()->confidence, 2.0);
  expectCode(leaf::detail::detectCenterVein(grayImage(gray), contour, above),
             leaf::ErrorCode::CentralVeinNotFound);
}

TEST(center_vein, CoversDeltaCurveRotationAndSourceScale) {
  cv::Mat parallel(220, 220, CV_8UC1, cv::Scalar(255));
  const std::vector<cv::Point> trapezoid = {{40, 4}, {160, 4}, {190, 210}, {30, 210}};
  cv::fillPoly(parallel, std::vector<std::vector<cv::Point>>{trapezoid}, cv::Scalar(160));
  cv::line(parallel, {80, 20}, {80, 180}, cv::Scalar(20), 1, cv::LINE_8);
  cv::line(parallel, {120, 20}, {120, 180}, cv::Scalar(20), 1, cv::LINE_8);
  const auto contour = polygonContour({{40, 4}, {160, 4}, {190, 210}, {30, 210}});
  auto ambiguous = veinConfig();
  ambiguous.ambiguityScoreDelta = 0.5;
  expectCode(leaf::detail::detectCenterVein(grayImage(parallel), contour, ambiguous),
             leaf::ErrorCode::AmbiguousCentralVein);
  ambiguous.ambiguityScoreDelta = 0;
  ASSERT_TRUE(leaf::detail::detectCenterVein(grayImage(parallel), contour, ambiguous).hasValue());

  cv::Mat curved(220, 220, CV_8UC1, cv::Scalar(255));
  cv::fillPoly(curved, std::vector<std::vector<cv::Point>>{trapezoid}, cv::Scalar(160));
  for (int y = 10; y <= 190; ++y) {
    const double t = static_cast<double>(y - 10) / 180.0;
    const int x = 100 + static_cast<int>(std::lround(30.0 * std::sin(t * 3.141592653589793)));
    curved.at<std::uint8_t>(y, x) = 20;
  }
  const auto arc = leaf::detail::detectCenterVein(grayImage(curved), contour, veinConfig());
  ASSERT_TRUE(arc.hasValue()) << arc.error()->message;
  EXPECT_GT(pathLength(arc.value()->path), 180.0);
  EXPECT_NEAR(arc.value()->base.y, 190, 8);
  EXPECT_LT(arc.value()->apex.y, arc.value()->base.y);

  cv::Mat turned(240, 240, CV_8UC1, cv::Scalar(255));
  const std::vector<cv::Point> horizontal = {{10, 40}, {200, 90}, {200, 130}, {10, 180}};
  cv::fillPoly(turned, std::vector<std::vector<cv::Point>>{horizontal}, cv::Scalar(160));
  cv::line(turned, {30, 110}, {190, 110}, cv::Scalar(20), 1, cv::LINE_8);
  const auto turnedContour = polygonContour({{10, 40}, {200, 90}, {200, 130}, {10, 180}});
  const auto rotated = leaf::detail::detectCenterVein(grayImage(turned), turnedContour, veinConfig());
  ASSERT_TRUE(rotated.hasValue()) << rotated.error()->message;
  EXPECT_LT(rotated.value()->base.x, rotated.value()->apex.x);
  EXPECT_NEAR(rotated.value()->base.y, 110, 8);
  EXPECT_NEAR(rotated.value()->apex.y, 110, 8);

  cv::Mat gray(220, 220, CV_8UC1, cv::Scalar(255));
  cv::fillPoly(gray, std::vector<std::vector<cv::Point>>{trapezoid}, cv::Scalar(160));
  cv::line(gray, {100, 10}, {100, 190}, cv::Scalar(20), 1, cv::LINE_8);
  auto scaled = grayImage(gray);
  scaled.transform.sourcePerWorkingX = 2;
  scaled.transform.sourcePerWorkingY = 2;
  scaled.transform.sourceWidth = 440;
  scaled.transform.sourceHeight = 440;
  const auto source = leaf::detail::detectCenterVein(scaled, contour, veinConfig());
  ASSERT_TRUE(source.hasValue()) << source.error()->message;
  EXPECT_NEAR(source.value()->base.x, 200, 10);
  EXPECT_NEAR(source.value()->base.y, 380, 10);
  EXPECT_NEAR(source.value()->apex.x, 200, 10);
  EXPECT_NEAR(source.value()->apex.y, 20, 10);
}
