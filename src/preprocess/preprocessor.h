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

Outcome<PreprocessResult> preprocess(ImageView view, const PreprocessConfig& config) noexcept;

}  // namespace leaf::detail
