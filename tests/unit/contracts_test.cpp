#include <gtest/gtest.h>

#include <leaf/analyzer.h>
#include <leaf/config.h>
#include <leaf/error.h>
#include <leaf/image.h>

#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {

leaf::ErrorCode errorCode(const leaf::Outcome<leaf::AnalyzerConfig>& outcome) {
  EXPECT_NE(outcome.error(), nullptr);
  return outcome.error()->code;
}

leaf::ErrorCode errorCode(const leaf::Outcome<leaf::ImageView>& outcome) {
  EXPECT_NE(outcome.error(), nullptr);
  return outcome.error()->code;
}

leaf::AnalyzerConfig validConfig() { return leaf::defaultAnalyzerConfig(); }

}  // namespace

TEST(Contracts, DefaultsAndImageValidationAreExact) {
  const leaf::AnalyzerConfig config{};
  EXPECT_EQ(config.preprocess.targetMaxDimension, 2048);
  EXPECT_DOUBLE_EQ(config.detection.ambiguityScoreDelta, .03);
  EXPECT_FALSE(leaf::validateImage({nullptr, 1, 1, 3, leaf::PixelFormat::RGB8}).hasValue());
  auto bad = config;
  bad.detection.minLeafAreaRatio = bad.detection.maxLeafAreaRatio;
  ASSERT_FALSE(leaf::validateConfig(bad).hasValue());
  EXPECT_EQ(errorCode(leaf::validateConfig(bad)), leaf::ErrorCode::InvalidConfigValue);
}

static_assert(noexcept(leaf::Analyzer::create()));
static_assert(noexcept(std::declval<const leaf::Analyzer&>().analyze({})));

TEST(Contracts, DefaultConfigAndCreateAreValid) {
  const leaf::AnalyzerConfig config{};
  ASSERT_TRUE(leaf::validateConfig(config).hasValue());
  ASSERT_TRUE(leaf::Analyzer::create().hasValue());
  ASSERT_TRUE(leaf::Analyzer::create({}).hasValue());

  EXPECT_DOUBLE_EQ(config.scoreRanges[0].min, 0.0);
  EXPECT_DOUBLE_EQ(*config.scoreRanges[0].max, 0.040);
  EXPECT_EQ(config.scoreRanges[0].score, 1);
  EXPECT_DOUBLE_EQ(config.scoreRanges[1].min, 0.040);
  EXPECT_DOUBLE_EQ(config.scoreRanges[4].min, 0.055);
  EXPECT_FALSE(config.scoreRanges[4].max.has_value());
  EXPECT_EQ(config.scoreRanges[4].score, 5);
}

TEST(Contracts, RejectsInvalidScoreRanges) {
  auto config = validConfig();
  config.scoreRanges[2].score = 9;
  const auto result = leaf::validateConfig(config);
  ASSERT_FALSE(result.hasValue());
  EXPECT_EQ(result.error()->code, leaf::ErrorCode::InvalidScoreRanges);
  EXPECT_EQ(result.error()->stage, leaf::Stage::Score);
}

TEST(Contracts, RejectsEvenBlurKernel) {
  auto config = validConfig();
  config.preprocess.blurKernel = 4;
  const auto result = leaf::validateConfig(config);
  ASSERT_FALSE(result.hasValue());
  EXPECT_EQ(errorCode(result), leaf::ErrorCode::InvalidConfigValue);
  EXPECT_EQ(result.error()->stage, leaf::Stage::Config);
}

TEST(Contracts, RejectsEvenVeinThresholdBlockSize) {
  auto config = validConfig();
  config.detection.veinThresholdBlockSize = 30;
  const auto result = leaf::validateConfig(config);
  ASSERT_FALSE(result.hasValue());
  EXPECT_EQ(errorCode(result), leaf::ErrorCode::InvalidConfigValue);
}

TEST(Contracts, RejectsProcessContrastStricterThanAcceptance) {
  auto config = validConfig();
  config.preprocess.minProcessLuminanceStdDev = 20.0;
  config.preprocess.minAcceptableLuminanceStdDev = 12.0;
  const auto result = leaf::validateConfig(config);
  ASSERT_FALSE(result.hasValue());
  EXPECT_EQ(errorCode(result), leaf::ErrorCode::InvalidConfigValue);
}

TEST(Contracts, RejectsQualityBelowMinimumStageConfidence) {
  auto config = validConfig();
  config.detection.minimumStageConfidence = 0.50;
  config.quality.minimumLeafConfidence = 0.49;
  const auto result = leaf::validateConfig(config);
  ASSERT_FALSE(result.hasValue());
  EXPECT_EQ(errorCode(result), leaf::ErrorCode::InvalidConfigValue);
}

TEST(Contracts, AcceptsValidImageView) {
  std::array<std::uint8_t, 12> data{};
  const auto result =
      leaf::validateImage({data.data(), 2, 2, 6, leaf::PixelFormat::RGB8});
  ASSERT_TRUE(result.hasValue());
  EXPECT_EQ(result.value()->data, data.data());
}

TEST(Contracts, RejectsUnsupportedPixelFormat) {
  const auto result =
      leaf::validateImage({nullptr, 1, 1, 1, static_cast<leaf::PixelFormat>(99)});
  ASSERT_FALSE(result.hasValue());
  EXPECT_EQ(errorCode(result), leaf::ErrorCode::UnsupportedPixelFormat);
  EXPECT_EQ(result.error()->stage, leaf::Stage::Input);
}

TEST(Contracts, RejectsStrideOverflow) {
  std::array<std::uint8_t, 4> data{};
  const auto result = leaf::validateImage(
      {data.data(), std::numeric_limits<std::int32_t>::max(), 1,
       std::numeric_limits<std::int32_t>::max(), leaf::PixelFormat::RGB8});
  ASSERT_FALSE(result.hasValue());
  EXPECT_EQ(errorCode(result), leaf::ErrorCode::InvalidImage);
  EXPECT_EQ(result.error()->message, "stride/overflow");
}

TEST(Contracts, RejectsUndersizedStride) {
  std::array<std::uint8_t, 2> data{};
  const auto result =
      leaf::validateImage({data.data(), 1, 1, 2, leaf::PixelFormat::RGB8});
  ASSERT_FALSE(result.hasValue());
  EXPECT_EQ(errorCode(result), leaf::ErrorCode::InvalidImage);
}

TEST(Contracts, PublicHeadersDoNotReferenceForbiddenPatterns) {
  const std::string root = CMAKE_SOURCE_DIR;
  const std::string files[] = {
      root + "/include/leaf/analyzer.h",
      root + "/include/leaf/config.h",
      root + "/include/leaf/detectors.h",
      root + "/include/leaf/error.h",
      root + "/include/leaf/image.h",
      root + "/include/leaf/json.h",
      root + "/include/leaf/outcome.h",
      root + "/include/leaf/result.h",
      root + "/include/leaf/types.h",
      root + "/include/leaf/debug.h",
      root + "/include/leaf/c/leaf.h",
  };

  for (const auto& path : files) {
    std::ifstream input(path);
    ASSERT_TRUE(input.is_open()) << path;
    const std::string content((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
    for (const char* forbidden : {"opencv", "cv::", "ONNX", "enableMlFallback"}) {
      EXPECT_EQ(content.find(forbidden), std::string::npos) << path << " mentions " << forbidden;
    }
  }
}
