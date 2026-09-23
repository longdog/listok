#pragma once

#include <leaf/config.h>
#include <leaf/image.h>
#include <leaf/outcome.h>

#include <opencv2/core.hpp>

namespace leaf::detail {

struct Photometry {
  double meanLuminance{};
  double luminanceStdDev{};
  bool acceptableContrast{};
  bool acceptableLighting{};
};

struct PreprocessResult {
  cv::Mat gray;
  cv::Mat binary;
  ResizeTransform transform;
  Photometry photometry;
};

// Photometry is measured on the resized working grayscale before illumination
// normalization, blur, or thresholding. `gray` and `binary` are always owned
// buffers that do not alias the caller's input pixels.
Outcome<PreprocessResult> preprocess(ImageView view, const PreprocessConfig& config) noexcept;

}  // namespace leaf::detail
