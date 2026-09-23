#pragma once

#include <leaf/outcome.h>
#include <leaf/types.h>

#include <cstdint>

namespace leaf {

enum class PixelFormat { Gray8, RGB8, RGBA8 };

struct ImageView {
  const std::uint8_t* data{};
  std::int32_t width{};
  std::int32_t height{};
  std::int32_t stride{};
  PixelFormat format{PixelFormat::RGB8};
};

struct ResizeTransform {
  std::int32_t sourceWidth{};
  std::int32_t sourceHeight{};
  std::int32_t workingWidth{};
  std::int32_t workingHeight{};
  double sourcePerWorkingX{1};
  double sourcePerWorkingY{1};

  Point toSource(Point p) const noexcept;
  Point toWorking(Point p) const noexcept;
};

Outcome<ImageView> validateImage(ImageView view) noexcept;

}  // namespace leaf
