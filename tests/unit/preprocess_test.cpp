#include <gtest/gtest.h>

#include <leaf/config.h>
#include <leaf/error.h>
#include <leaf/image.h>
#include <leaf/outcome.h>

#include "preprocessor.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdint>
#include <cstring>
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

OwnedImage solidRgb(int width, int height, std::uint8_t value) {
  OwnedImage image(leaf::PixelFormat::RGB8, width, height, width * 3);
  for (int i = 0; i < width * height; ++i) {
    image.pixels[static_cast<std::size_t>(i) * 3 + 0] = value;
    image.pixels[static_cast<std::size_t>(i) * 3 + 1] = value;
    image.pixels[static_cast<std::size_t>(i) * 3 + 2] = value;
  }
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

OwnedImage grayWithPaddedStride(int width, int height, int pad, std::uint8_t value) {
  const int stride = width + pad;
  OwnedImage image(leaf::PixelFormat::Gray8, width, height, stride);
  for (int y = 0; y < height; ++y) {
    std::fill(image.pixels.begin() + static_cast<std::size_t>(y) * stride,
              image.pixels.begin() + static_cast<std::size_t>(y) * stride + width, value);
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

OwnedImage texturedGray(int width, int height, std::uint8_t mean, int amplitude) {
  return noisyGray(width, height, mean, amplitude);
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

OwnedImage texturedRgba(int width, int height, std::uint8_t r, std::uint8_t g, std::uint8_t b,
                        int amplitude) {
  OwnedImage image(leaf::PixelFormat::RGBA8, width, height, width * 4);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const int delta = ((x + y) % 2 == 0) ? amplitude : -amplitude;
      const auto base = (static_cast<std::size_t>(y) * width + x) * 4;
      image.pixels[base + 0] =
          static_cast<std::uint8_t>(std::clamp(static_cast<int>(r) + delta, 0, 255));
      image.pixels[base + 1] =
          static_cast<std::uint8_t>(std::clamp(static_cast<int>(g) + delta, 0, 255));
      image.pixels[base + 2] =
          static_cast<std::uint8_t>(std::clamp(static_cast<int>(b) + delta, 0, 255));
      image.pixels[base + 3] = 255;
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

std::uint64_t checksum(const std::vector<std::uint8_t>& data) {
  std::uint64_t sum = 0;
  for (std::uint8_t byte : data) {
    sum += byte;
  }
  return sum;
}

leaf::ErrorCode errorCode(const leaf::Outcome<leaf::detail::PreprocessResult>& outcome) {
  EXPECT_FALSE(outcome.hasValue());
  EXPECT_NE(outcome.error(), nullptr);
  return outcome.error()->code;
}

}  // namespace

TEST(Preprocess, PreservesAspectAndClassifiesPhotometry) {
  auto rgb = texturedRgb(4000, 2000, 128, 60, 8);
  auto ok = leaf::detail::preprocess(rgb.view, {});
  ASSERT_TRUE(ok.hasValue());
  EXPECT_EQ(ok.value()->gray.cols, 2048);
  EXPECT_EQ(ok.value()->gray.rows, 1024);
  EXPECT_DOUBLE_EQ(ok.value()->transform.sourcePerWorkingX, 4000.0 / 2048.0);
  auto flat = solidGray(64, 64, 128);
  EXPECT_EQ(errorCode(leaf::detail::preprocess(flat.view, {})), leaf::ErrorCode::InsufficientContrast);
  auto dark = darkLowLighting(64, 64);
  EXPECT_EQ(errorCode(leaf::detail::preprocess(dark.view, {})), leaf::ErrorCode::InsufficientLighting);
}

TEST(Preprocess, ConvertsAllPixelFormatsWithRgbOrder) {
  leaf::PreprocessConfig config{};
  config.normalizeIllumination = false;

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

TEST(Preprocess, HandlesPaddedStride) {
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
  leaf::PreprocessConfig config{};
  config.normalizeIllumination = false;
  const auto ok = leaf::detail::preprocess(padded.view, config);
  ASSERT_TRUE(ok.hasValue());
  EXPECT_EQ(ok.value()->gray.cols, 48);
  EXPECT_EQ(ok.value()->gray.rows, 32);
  EXPECT_NEAR(ok.value()->photometry.meanLuminance, 100.0, 0.5);
}

TEST(Preprocess, IdentityTransformWhenNoResizeNeeded) {
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

TEST(Preprocess, ResizeRoundTripMapsCoordinatesExactly) {
  auto image = texturedGray(1000, 500, 140, 22);
  const auto ok = leaf::detail::preprocess(image.view, {});
  ASSERT_TRUE(ok.hasValue());
  const auto& t = ok.value()->transform;
  const leaf::Point working{123.5, 67.25};
  const leaf::Point source = t.toSource(working);
  const leaf::Point back = t.toWorking(source);
  EXPECT_NEAR(back.x, working.x, 1e-9);
  EXPECT_NEAR(back.y, working.y, 1e-9);
}

TEST(Preprocess, PreservesInputImmutability) {
  auto image = texturedRgb(128, 64, 90, 16);
  const auto before = checksum(image.pixels);
  const auto ok = leaf::detail::preprocess(image.view, {});
  ASSERT_TRUE(ok.hasValue());
  EXPECT_EQ(checksum(image.pixels), before);
}

TEST(Preprocess, RejectsMeanAtProcessingBoundaries) {
  auto low = grayWithStdDev(64, 64, 7.0, 10.0);
  auto high = grayWithStdDev(64, 64, 246.0, 8.0);
  EXPECT_EQ(errorCode(leaf::detail::preprocess(low.view, {})), leaf::ErrorCode::InsufficientLighting);
  EXPECT_EQ(errorCode(leaf::detail::preprocess(high.view, {})), leaf::ErrorCode::InsufficientLighting);
}

TEST(Preprocess, ClassifiesAcceptanceAtStdDevBoundaries) {
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

TEST(Preprocess, UsesIntegerResizeRounding) {
  auto image = texturedGray(333, 111, 100, 14);
  const auto ok = leaf::detail::preprocess(image.view, {});
  ASSERT_TRUE(ok.hasValue());
  EXPECT_EQ(ok.value()->gray.cols, 333);
  EXPECT_EQ(ok.value()->gray.rows, 111);
}

TEST(Preprocess, ProducesBinaryOutput) {
  auto image = noisyGray(128, 128, 128, 40);
  const auto ok = leaf::detail::preprocess(image.view, {});
  ASSERT_TRUE(ok.hasValue());
  EXPECT_EQ(ok.value()->binary.type(), CV_8UC1);
  EXPECT_EQ(ok.value()->binary.rows, ok.value()->gray.rows);
  EXPECT_EQ(ok.value()->binary.cols, ok.value()->gray.cols);
  const double uniqueValues = cv::countNonZero(ok.value()->binary) +
                              cv::countNonZero(255 - ok.value()->binary);
  EXPECT_GT(uniqueValues, 0.0);
}

TEST(Preprocess, IsDeterministicAcrossRepeatedRuns) {
  auto image = noisyGray(96, 96, 128, 35);
  const auto first = leaf::detail::preprocess(image.view, {});
  const auto second = leaf::detail::preprocess(image.view, {});
  ASSERT_TRUE(first.hasValue());
  ASSERT_TRUE(second.hasValue());
  EXPECT_EQ(cv::countNonZero(first.value()->binary != second.value()->binary), 0);
  EXPECT_DOUBLE_EQ(first.value()->photometry.meanLuminance, second.value()->photometry.meanLuminance);
}
