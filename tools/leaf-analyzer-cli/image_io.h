#pragma once

#include <leaf/image.h>

#include <string>
#include <vector>

namespace leaf::cli {

struct LoadedImage {
  std::vector<unsigned char> pixels;
  ImageView view;
};

enum class LoadStatus {
  Ok,
  UnsupportedType,
  Corrupt,
};

LoadStatus loadImage(const std::string& path, LoadedImage& image);

}  // namespace leaf::cli
