#include <gtest/gtest.h>

#include <leaf/config.h>
#include <leaf/error.h>
#include <leaf/image.h>
#include <leaf/outcome.h>

#include "preprocessor.h"

#include <opencv2/core.hpp>

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

namespace {

struct OwnedImage {
  std::vector<std::uint8_t> pixels;
  leaf::ImageView view{};

  OwnedImage(leaf::PixelFormat format, int width, int height, int stride) {
    pixels.resize(static_cast<std::size_t>(stride) * static_cast<std::size_t>(height));
    view = {pixels.data(), width, height, stride, format};
  }
};

OwnedImage solidGray(int width, int height, std::uint8_t value) {
  OwnedImage image(leaf::PixelFormat::Gray8, width, height, width);
  std::fill(image.pixels.begin(), image.pixels.end(), value);
  return image;
}

OwnedImage solidRgba(int width, int height, std::uint8_t r, std::uint8_t g, std::uint8_t b,
                     std::uint8_t a = 255) {
  OwnedImage image(leaf::PixelFormat::RGBA8, width, height, width * 4);
  for (int i = 0; i < width * height; ++i) {
    const auto base = static_cast<std::size_t>(i) * 4;
    image.pixels[base + 0] = r;
    image.pixels[base + 1] = g;
    image.pixels[base + 2] = b;
    image.pixels[base + 3] = a;
  }
  return image;
}

OwnedImage darkLowLighting(int width, int height) {
  OwnedImage image(leaf::PixelFormat::Gray8, width, height, width);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      image.pixels[static_cast<std::size_t>(y) * width + x] =
          ((x + y) % 4 == 0) ? 20 : 0;
    }
  }
  return image;
}

OwnedImage noisyGray(int width, int height, std::uint8_t mean, int amplitude) {
  OwnedImage image(leaf::PixelFormat::Gray8, width, height, width);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const int delta = ((x + y) % 2 == 0) ? amplitude : -amplitude;
      const int value = static_cast<int>(mean) + delta;
      image.pixels[static_cast<std::size_t>(y) * width + x] =
          static_cast<std::uint8_t>(std::clamp(value, 0, 255));
    }
  }
  return image;
}

OwnedImage texturedGray(int width, int height, std::uint8_t mean, int amplitude, int block = 1) {
  OwnedImage image(leaf::PixelFormat::Gray8, width, height, width);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const int delta = ((x / block + y / block) % 2 == 0) ? amplitude : -amplitude;
      image.pixels[static_cast<std::size_t>(y) * width + x] = static_cast<std::uint8_t>(
          std::clamp(static_cast<int>(mean) + delta, 0, 255));
    }
  }
  return image;
}

OwnedImage texturedRgb(int width, int height, std::uint8_t mean, int amplitude, int block = 1) {
  OwnedImage image(leaf::PixelFormat::RGB8, width, height, width * 3);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const int delta = ((x / block + y / block) % 2 == 0) ? amplitude : -amplitude;
      const auto value = static_cast<std::uint8_t>(
          std::clamp(static_cast<int>(mean) + delta, 0, 255));
      const auto base = (static_cast<std::size_t>(y) * width + x) * 3;
      image.pixels[base + 0] = value;
      image.pixels[base + 1] = value;
      image.pixels[base + 2] = value;
    }
  }
  return image;
}

OwnedImage grayWithStdDev(int width, int height, double targetMean, double targetStdDev) {
  OwnedImage image(leaf::PixelFormat::Gray8, width, height, width);
  const int low = static_cast<int>(std::round(targetMean - targetStdDev));
  const int high = static_cast<int>(std::round(targetMean + targetStdDev));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const int value = ((x + y) % 2 == 0) ? low : high;
      image.pixels[static_cast<std::size_t>(y) * width + x] =
          static_cast<std::uint8_t>(std::clamp(value, 0, 255));
    }
  }
  return image;
}

OwnedImage darkObjectOnLightBackground(int width, int height, std::uint8_t background,
                                      std::uint8_t object, int objectRadius) {
  OwnedImage image(leaf::PixelFormat::Gray8, width, height, width);
  const int centerX = width / 2;
  const int centerY = height / 2;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const int dx = x - centerX;
      const int dy = y - centerY;
      const bool inside = (dx * dx + dy * dy) <= (objectRadius * objectRadius);
      image.pixels[static_cast<std::size_t>(y) * width + x] = inside ? object : background;
    }
  }
  return image;
}

OwnedImage illuminationSpotImage() {
  return darkObjectOnLightBackground(128, 128, 200, 50, 24);
}

std::uint64_t checksum(const std::vector<std::uint8_t>& data) {
  return std::accumulate(data.begin(), data.end(), std::uint64_t{0});
}

template <typename T>
void expectError(const leaf::Outcome<T>& outcome, leaf::ErrorCode expected) {
  ASSERT_FALSE(outcome.hasValue());
  ASSERT_NE(outcome.error(), nullptr);
  EXPECT_EQ(outcome.error()->code, expected);
}

leaf::PreprocessConfig passthroughConfig() {
  leaf::PreprocessConfig config{};
  config.normalizeIllumination = false;
  config.blurKernel = 1;
  return config;
}

void expectStrictBinaryMask(const cv::Mat& binary) {
  ASSERT_EQ(binary.type(), CV_8UC1);
  const cv::Mat invalid = (binary != 0) & (binary != 255);
  EXPECT_EQ(cv::countNonZero(invalid), 0);
  EXPECT_GT(cv::countNonZero(binary == 0), 0);
  EXPECT_GT(cv::countNonZero(binary == 255), 0);
}

}  // namespace

TEST(preprocess, PreservesAspectAndClassifiesPhotometry) {
  auto rgb = texturedRgb(4000, 2000, 128, 60, 8);
  auto ok = leaf::detail::preprocess(rgb.view, {});
  ASSERT_TRUE(ok.hasValue());
  EXPECT_EQ(ok.value()->gray.cols, 2048);
  EXPECT_EQ(ok.value()->gray.rows, 1024);
  EXPECT_DOUBLE_EQ(ok.value()->transform.sourcePerWorkingX, 4000.0 / 2048.0);
  auto flat = solidGray(64, 64, 128);
  expectError(leaf::detail::preprocess(flat.view, {}), leaf::ErrorCode::InsufficientContrast);
  auto dark = darkLowLighting(64, 64);
  expectError(leaf::detail::preprocess(dark.view, {}), leaf::ErrorCode::InsufficientLighting);
}

TEST(preprocess, ConvertsAllPixelFormatsWithRgbOrder) {
  const auto config = passthroughConfig();

  auto gray = texturedGray(32, 32, 80, 20);
  auto rgb = texturedRgb(32, 32, 80, 20);
  auto rgba = solidRgba(32, 32, 255, 0, 0);
  for (int y = 0; y < 32; ++y) {
    for (int x = 0; x < 32; ++x) {
      const auto base = (static_cast<std::size_t>(y) * 32 + x) * 4;
      rgba.pixels[base + 0] = ((x + y) % 2 == 0) ? 255 : 0;
      rgba.pixels[base + 1] = 0;
      rgba.pixels[base + 2] = 0;
    }
  }

  const auto grayOk = leaf::detail::preprocess(gray.view, config);
  const auto rgbOk = leaf::detail::preprocess(rgb.view, config);
  const auto rgbaOk = leaf::detail::preprocess(rgba.view, config);
  ASSERT_TRUE(grayOk.hasValue());
  ASSERT_TRUE(rgbOk.hasValue());
  ASSERT_TRUE(rgbaOk.hasValue());

  EXPECT_NEAR(grayOk.value()->photometry.meanLuminance, 80.0, 0.5);
  EXPECT_NEAR(rgbOk.value()->photometry.meanLuminance, 80.0, 0.5);
  EXPECT_NEAR(rgbaOk.value()->photometry.meanLuminance, 38.1225, 0.5);
}

TEST(preprocess, HandlesPaddedStride) {
  const int width = 48;
  const int height = 32;
  const int stride = width + 8;
  OwnedImage padded(leaf::PixelFormat::Gray8, width, height, stride);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const int delta = ((x + y) % 2 == 0) ? 15 : -15;
      padded.pixels[static_cast<std::size_t>(y) * stride + x] =
          static_cast<std::uint8_t>(std::clamp(100 + delta, 0, 255));
    }
  }
  const auto config = passthroughConfig();
  const auto ok = leaf::detail::preprocess(padded.view, config);
  ASSERT_TRUE(ok.hasValue());
  EXPECT_EQ(ok.value()->gray.cols, 48);
  EXPECT_EQ(ok.value()->gray.rows, 32);
  EXPECT_NEAR(ok.value()->photometry.meanLuminance, 100.0, 0.5);
}

TEST(preprocess, IdentityTransformWhenNoResizeNeeded) {
  auto image = texturedGray(512, 256, 120, 18);
  const auto ok = leaf::detail::preprocess(image.view, {});
  ASSERT_TRUE(ok.hasValue());
  EXPECT_EQ(ok.value()->gray.cols, 512);
  EXPECT_EQ(ok.value()->gray.rows, 256);
  EXPECT_DOUBLE_EQ(ok.value()->transform.sourcePerWorkingX, 1.0);
  EXPECT_DOUBLE_EQ(ok.value()->transform.sourcePerWorkingY, 1.0);
  EXPECT_EQ(ok.value()->transform.workingWidth, 512);
  EXPECT_EQ(ok.value()->transform.sourceWidth, 512);
}

TEST(preprocess, ResizeRoundTripMapsCoordinatesExactly) {
  auto image = texturedGray(2500, 1000, 140, 60, 8);
  const auto ok = leaf::detail::preprocess(image.view, {});
  ASSERT_TRUE(ok.hasValue());
  EXPECT_EQ(ok.value()->gray.cols, 2048);
  EXPECT_EQ(ok.value()->gray.rows, 819);
  const auto& t = ok.value()->transform;
  EXPECT_NE(t.sourcePerWorkingX, 1.0);
  const leaf::Point working{123.5, 67.25};
  const leaf::Point source = t.toSource(working);
  const leaf::Point back = t.toWorking(source);
  EXPECT_NEAR(back.x, working.x, 1e-9);
  EXPECT_NEAR(back.y, working.y, 1e-9);
}

TEST(preprocess, UsesIntegerResizeRounding) {
  auto image = texturedGray(2500, 1000, 100, 60, 8);
  const auto ok = leaf::detail::preprocess(image.view, {});
  ASSERT_TRUE(ok.hasValue());
  EXPECT_EQ(ok.value()->gray.cols, 2048);
  EXPECT_EQ(ok.value()->gray.rows, 819);
}

TEST(preprocess, GrayOutputIsOwnedAndWritable) {
  auto image = texturedGray(64, 64, 120, 16);
  auto config = passthroughConfig();
  const auto before = checksum(image.pixels);
  auto ok = leaf::detail::preprocess(image.view, config);
  ASSERT_TRUE(ok.hasValue());
  auto* result = ok.value();
  ASSERT_NE(result, nullptr);
  EXPECT_NE(result->gray.data, image.view.data);
  *result->gray.ptr<std::uint8_t>(0) = 99;
  EXPECT_EQ(checksum(image.pixels), before);
}

TEST(preprocess, PreservesInputImmutability) {
  auto image = texturedRgb(128, 64, 90, 16);
  const auto before = checksum(image.pixels);
  const auto ok = leaf::detail::preprocess(image.view, {});
  ASSERT_TRUE(ok.hasValue());
  EXPECT_EQ(checksum(image.pixels), before);
}

TEST(preprocess, AcceptsInclusiveProcessingMeanBoundaries) {
  auto low = grayWithStdDev(64, 64, 10.0, 10.0);
  auto high = grayWithStdDev(64, 64, 245.0, 10.0);
  EXPECT_TRUE(leaf::detail::preprocess(low.view, {}).hasValue());
  EXPECT_TRUE(leaf::detail::preprocess(high.view, {}).hasValue());
}

TEST(preprocess, RejectsMeanOutsideProcessingRange) {
  auto low = grayWithStdDev(64, 64, 9.0, 10.0);
  auto high = grayWithStdDev(64, 64, 246.0, 8.0);
  expectError(leaf::detail::preprocess(low.view, {}), leaf::ErrorCode::InsufficientLighting);
  expectError(leaf::detail::preprocess(high.view, {}), leaf::ErrorCode::InsufficientLighting);
}

TEST(preprocess, ClassifiesAcceptanceAtStdDevBoundaries) {
  auto borderlineContrast = grayWithStdDev(64, 64, 128.0, 12.0);
  auto ok = leaf::detail::preprocess(borderlineContrast.view, {});
  ASSERT_TRUE(ok.hasValue());
  EXPECT_NEAR(ok.value()->photometry.luminanceStdDev, 12.0, 0.5);
  EXPECT_TRUE(ok.value()->photometry.acceptableContrast);
  EXPECT_TRUE(ok.value()->photometry.acceptableLighting);

  auto belowAcceptable = grayWithStdDev(64, 64, 128.0, 8.0);
  ok = leaf::detail::preprocess(belowAcceptable.view, {});
  ASSERT_TRUE(ok.hasValue());
  EXPECT_NEAR(ok.value()->photometry.luminanceStdDev, 8.0, 0.5);
  EXPECT_FALSE(ok.value()->photometry.acceptableContrast);
  EXPECT_TRUE(ok.value()->photometry.acceptableLighting);
}

TEST(preprocess, NormalizesDarkSpotBelowMidpoint) {
  auto image = illuminationSpotImage();
  leaf::PreprocessConfig config{};
  config.normalizeIllumination = true;
  config.blurKernel = 1;
  const auto ok = leaf::detail::preprocess(image.view, config);
  ASSERT_TRUE(ok.hasValue());
  const cv::Rect center(40, 40, 48, 48);
  EXPECT_LT(cv::mean(ok.value()->gray(center))[0], 128.0);
}

TEST(preprocess, ProducesStrictBinaryMask) {
  auto image = darkObjectOnLightBackground(128, 128, 220, 40, 28);
  const auto ok = leaf::detail::preprocess(image.view, passthroughConfig());
  ASSERT_TRUE(ok.hasValue());
  expectStrictBinaryMask(ok.value()->binary);
}

TEST(preprocess, BinaryMaskMarksDarkObjectAsForeground) {
  auto image = darkObjectOnLightBackground(128, 128, 220, 40, 28);
  leaf::PreprocessConfig config = passthroughConfig();
  const auto ok = leaf::detail::preprocess(image.view, config);
  ASSERT_TRUE(ok.hasValue());
  expectStrictBinaryMask(ok.value()->binary);
  const cv::Rect objectRegion(32, 32, 64, 64);
  const cv::Rect backgroundRegion(0, 0, 16, 16);
  EXPECT_GT(cv::mean(ok.value()->binary(objectRegion))[0], cv::mean(ok.value()->binary(backgroundRegion))[0]);
}

TEST(preprocess, UsesAdaptiveThresholdConfig) {
  auto image = noisyGray(96, 96, 128, 35);
  leaf::PreprocessConfig config = passthroughConfig();
  config.adaptiveThreshold = true;
  config.adaptiveThresholdBlockSize = 31;
  config.adaptiveThresholdC = 2.0;
  const auto baseline = leaf::detail::preprocess(image.view, config);
  ASSERT_TRUE(baseline.hasValue());
  expectStrictBinaryMask(baseline.value()->binary);

  config.adaptiveThresholdBlockSize = 3;
  config.adaptiveThresholdC = -64.0;
  EXPECT_TRUE(leaf::detail::preprocess(image.view, config).hasValue());

  config.adaptiveThresholdBlockSize = 255;
  config.adaptiveThresholdC = 64.0;
  EXPECT_TRUE(leaf::detail::preprocess(image.view, config).hasValue());
}

TEST(preprocess, RejectsInvalidPreprocessConfig) {
  auto image = texturedGray(32, 32, 120, 16);
  leaf::PreprocessConfig config{};
  config.adaptiveThresholdBlockSize = 30;
  const auto result = leaf::detail::preprocess(image.view, config);
  expectError(result, leaf::ErrorCode::InvalidConfigValue);
  EXPECT_EQ(result.error()->stage, leaf::Stage::Config);
}

TEST(preprocess, IsDeterministicAcrossRepeatedRuns) {
  auto image = noisyGray(96, 96, 128, 35);
  const auto first = leaf::detail::preprocess(image.view, {});
  const auto second = leaf::detail::preprocess(image.view, {});
  ASSERT_TRUE(first.hasValue());
  ASSERT_TRUE(second.hasValue());
  EXPECT_EQ(cv::countNonZero(first.value()->binary != second.value()->binary), 0);
  EXPECT_DOUBLE_EQ(first.value()->photometry.meanLuminance, second.value()->photometry.meanLuminance);
}
