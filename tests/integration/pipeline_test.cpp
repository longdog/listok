#include <gtest/gtest.h>

#include <leaf/analyzer.h>
#include <leaf/json.h>

#include <cmath>
#include <fstream>
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

bool finiteSide(const leaf::SideMeasurements& side) {
  return std::isfinite(side.m1) && std::isfinite(side.m2) && std::isfinite(side.m3) &&
         std::isfinite(side.m4) && std::isfinite(side.m5);
}

}  // namespace

TEST(Pipeline, ProducesCompleteLabeledResult) {
  std::int32_t width = 0;
  std::int32_t height = 0;
  auto pixels = readPpm(std::string(CMAKE_SOURCE_DIR) + "/tests/fixtures/cli/leaf_cross.ppm", width, height);
  ASSERT_FALSE(pixels.empty());
  std::ifstream configFile(std::string(CMAKE_SOURCE_DIR) + "/tests/fixtures/cli/leaf_cross.config.json");
  std::stringstream configText;
  configText << configFile.rdbuf();
  const auto config = leaf::parseAnalyzerConfigJson(configText.str());
  ASSERT_TRUE(config.hasValue()) << config.error()->message;
  const auto analyzer = leaf::Analyzer::create(*config.value());
  ASSERT_TRUE(analyzer.hasValue()) << analyzer.error()->message;

  leaf::ImageView view{pixels.data(), width, height, width * 3, leaf::PixelFormat::RGB8};
  const auto result = analyzer.value()->get()->analyze(view);
  ASSERT_TRUE(result.hasValue()) << result.error()->message;
  EXPECT_TRUE(finiteSide(result.value()->measurements.left));
  EXPECT_TRUE(finiteSide(result.value()->measurements.right));
  EXPECT_TRUE(std::isfinite(result.value()->asymmetry.value));
  EXPECT_GE(result.value()->score, 1);
  EXPECT_LE(result.value()->score, 5);
}
