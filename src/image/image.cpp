#include <leaf/image.h>

#include <climits>
#include <cstddef>
#include <cstdint>

namespace leaf {

Point ResizeTransform::toSource(Point p) const noexcept {
  return {p.x * sourcePerWorkingX, p.y * sourcePerWorkingY};
}

Point ResizeTransform::toWorking(Point p) const noexcept {
  return {p.x / sourcePerWorkingX, p.y / sourcePerWorkingY};
}

Outcome<ImageView> validateImage(ImageView view) noexcept {
  const std::int64_t bytesPerPixel = view.format == PixelFormat::Gray8   ? 1
                                    : view.format == PixelFormat::RGB8   ? 3
                                    : view.format == PixelFormat::RGBA8  ? 4
                                                                         : 0;
  if (bytesPerPixel == 0) {
    return Outcome<ImageView>::failure(
        {ErrorCode::UnsupportedPixelFormat, Stage::Input, "pixel format"});
  }
  if (view.data == nullptr || view.width <= 0 || view.height <= 0) {
    return Outcome<ImageView>::failure(
        {ErrorCode::InvalidImage, Stage::Input, "dimensions/data"});
  }
  if (static_cast<std::int64_t>(view.width) > INT32_MAX / bytesPerPixel ||
      view.stride < static_cast<std::int64_t>(view.width) * bytesPerPixel ||
      static_cast<std::uint64_t>(view.stride) >
          SIZE_MAX / static_cast<std::uint64_t>(view.height)) {
    return Outcome<ImageView>::failure(
        {ErrorCode::InvalidImage, Stage::Input, "stride/overflow"});
  }
  return Outcome<ImageView>::success(view);
}

}  // namespace leaf
