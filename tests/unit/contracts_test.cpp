#include <gtest/gtest.h>

#include <leaf/analyzer.h>
#include <leaf/config.h>
#include <leaf/image.h>

#include <cctype>
#include <fstream>
#include <string>

namespace {

leaf::ErrorCode badErrorCode(const leaf::Outcome<leaf::AnalyzerConfig>& outcome) {
  EXPECT_NE(outcome.error(), nullptr);
  return outcome.error()->code;
}

}  // namespace

TEST(Contracts, DefaultsAndImageValidationAreExact) {
  const leaf::AnalyzerConfig c{};
  EXPECT_EQ(c.preprocess.targetMaxDimension, 2048);
  EXPECT_DOUBLE_EQ(c.detection.ambiguityScoreDelta, .03);
  EXPECT_FALSE(leaf::validateImage({nullptr, 1, 1, 3, leaf::PixelFormat::RGB8}).hasValue());
  auto bad = c;
  bad.detection.minLeafAreaRatio = bad.detection.maxLeafAreaRatio;
  ASSERT_FALSE(leaf::validateConfig(bad).hasValue());
  EXPECT_EQ(badErrorCode(leaf::validateConfig(bad)), leaf::ErrorCode::InvalidConfigValue);
}

static_assert(noexcept(leaf::Analyzer::create()));
static_assert(noexcept(std::declval<const leaf::Analyzer&>().analyze({})));

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
  };

  for (const auto& path : files) {
    std::ifstream input(path);
    ASSERT_TRUE(input.is_open()) << path;
    std::string content((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
    for (const char* forbidden : {"opencv", "cv::", "ONNX", "enableMlFallback"}) {
      EXPECT_EQ(content.find(forbidden), std::string::npos) << path << " mentions " << forbidden;
    }
  }
}
