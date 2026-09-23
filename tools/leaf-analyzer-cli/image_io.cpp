#include "image_io.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace leaf::cli {
namespace {

std::string extensionOf(const std::filesystem::path& path) {
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return extension;
}

bool supportedExtension(const std::filesystem::path& path) {
  const std::string extension = extensionOf(path);
  return extension == ".jpg" || extension == ".jpeg" || extension == ".png";
}

}  // namespace

LoadStatus loadImage(const std::string& path, LoadedImage& image) {
  image = {};
  const std::filesystem::path file(path);
  if (!supportedExtension(file)) {
    return LoadStatus::UnsupportedType;
  }

  std::ifstream input(file, std::ios::binary);
  if (!input) {
    return LoadStatus::Corrupt;
  }
  const std::vector<unsigned char> encoded((std::istreambuf_iterator<char>(input)),
                                           std::istreambuf_iterator<char>());
  if (encoded.empty()) {
    return LoadStatus::Corrupt;
  }

  const cv::Mat buffer(1, static_cast<int>(encoded.size()), CV_8UC1,
                       const_cast<unsigned char*>(encoded.data()));
  const cv::Mat decoded =
      cv::imdecode(buffer, cv::IMREAD_UNCHANGED | cv::IMREAD_IGNORE_ORIENTATION);
  if (decoded.empty() || decoded.dims != 2 || decoded.depth() != CV_8U) {
    return LoadStatus::Corrupt;
  }

  cv::Mat rgb;
  PixelFormat format = PixelFormat::RGB8;
  if (decoded.channels() == 1) {
    rgb = decoded;
    format = PixelFormat::Gray8;
  } else if (decoded.channels() == 3) {
    cv::cvtColor(decoded, rgb, cv::COLOR_BGR2RGB);
    format = PixelFormat::RGB8;
  } else if (decoded.channels() == 4) {
    cv::cvtColor(decoded, rgb, cv::COLOR_BGRA2RGBA);
    format = PixelFormat::RGBA8;
  } else {
    return LoadStatus::Corrupt;
  }
  if (!rgb.isContinuous()) {
    rgb = rgb.clone();
  }

  image.pixels.assign(rgb.datastart, rgb.dataend);
  image.view.data = image.pixels.data();
  image.view.width = rgb.cols;
  image.view.height = rgb.rows;
  image.view.stride = static_cast<std::int32_t>(rgb.cols * rgb.elemSize());
  image.view.format = format;
  return LoadStatus::Ok;
}

}  // namespace leaf::cli
