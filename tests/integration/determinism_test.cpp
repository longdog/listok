#include <gtest/gtest.h>

#include <leaf/analyzer.h>
#include <leaf/json.h>

#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> readPpm(const std::string& path, std::int32_t& width, std::int32_t& height) {
  std::ifstream input(path, std::ios::binary);
  std::string magic;
  int maxValue = 0;
  input >> magic >> width >> height >> maxValue;
  input.get();
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3u);
  input.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
  return pixels;
}

}  // namespace

TEST(Determinism, JsonIsBitIdenticalForOneHundredRuns) {
  std::int32_t width = 0;
  std::int32_t height = 0;
  const auto pixels =
      readPpm(std::string(CMAKE_SOURCE_DIR) + "/tests/fixtures/cli/leaf_cross.ppm", width, height);
  ASSERT_EQ(pixels.size(), static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3u);
  std::ifstream configFile(std::string(CMAKE_SOURCE_DIR) + "/tests/fixtures/cli/leaf_cross.config.json");
  std::stringstream configText;
  configText << configFile.rdbuf();
  const auto config = leaf::parseAnalyzerConfigJson(configText.str());
  ASSERT_TRUE(config.hasValue());
  const auto analyzer = leaf::Analyzer::create(*config.value());
  ASSERT_TRUE(analyzer.hasValue());
  const leaf::ImageView view{pixels.data(), width, height, width * 3, leaf::PixelFormat::RGB8};

  std::optional<std::string> first;
  for (int i = 0; i < 100; ++i) {
    const auto result = analyzer.value()->get()->analyze(view);
    ASSERT_TRUE(result.hasValue()) << result.error()->message;
    const auto json = leaf::analysisResultToJson(*result.value());
    ASSERT_TRUE(json.hasValue());
    if (!first) {
      first = *json.value();
    } else {
      ASSERT_EQ(*json.value(), *first) << i;
    }
  }
}
