#include "preprocessor.h"

#include "../image/resize.h"

#include <new>
#include <stdexcept>

#include <opencv2/imgproc.hpp>

namespace leaf::detail {
namespace {

cv::Mat viewToMat(const ImageView& view) {
  const int type = view.format == PixelFormat::Gray8   ? CV_8UC1
                 : view.format == PixelFormat::RGB8   ? CV_8UC3
                 : view.format == PixelFormat::RGBA8 ? CV_8UC4
                                                     : -1;
  return cv::Mat(view.height, view.width, type,
                 const_cast<std::uint8_t*>(view.data), static_cast<std::size_t>(view.stride));
}

cv::Mat toGray(const cv::Mat& source, PixelFormat format) {
  if (format == PixelFormat::Gray8) {
    return source;
  }
  cv::Mat gray;
  const int code = format == PixelFormat::RGB8 ? cv::COLOR_RGB2GRAY : cv::COLOR_RGBA2GRAY;
  cv::cvtColor(source, gray, code);
  return gray;
}

Photometry measurePhotometry(const cv::Mat& gray) {
  cv::Scalar mean{};
  cv::Scalar stddev{};
  cv::meanStdDev(gray, mean, stddev);
  Photometry photometry{};
  photometry.meanLuminance = mean[0];
  photometry.luminanceStdDev = stddev[0];
  return photometry;
}

cv::Mat normalizeIllumination(const cv::Mat& gray) {
  cv::Mat background;
  const int kernel = std::max(3, ((gray.cols + gray.rows) / 16) | 1);
  cv::GaussianBlur(gray, background, cv::Size(kernel, kernel), 0.0);
  cv::Mat normalized;
  cv::subtract(gray, background, normalized);
  cv::add(normalized, cv::Scalar(128.0), normalized);
  return normalized;
}

cv::Mat applyBlur(const cv::Mat& gray, int blurKernel) {
  if (blurKernel <= 1) {
    return gray;
  }
  cv::Mat blurred;
  cv::GaussianBlur(gray, blurred, cv::Size(blurKernel, blurKernel), 0.0);
  return blurred;
}

cv::Mat binarize(const cv::Mat& gray, const PreprocessConfig& config) {
  cv::Mat binary;
  if (config.adaptiveThreshold) {
    const int blockSize = std::max(3, (config.blurKernel * 6) | 1);
    cv::adaptiveThreshold(gray, binary, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY,
                          blockSize, 2.0);
    return binary;
  }
  cv::threshold(gray, binary, 0.0, 255.0, cv::THRESH_BINARY | cv::THRESH_OTSU);
  return binary;
}

Outcome<PreprocessResult> preprocessFailure(ErrorCode code, const char* message) {
  return Outcome<PreprocessResult>::failure({code, Stage::Preprocess, message});
}

}  // namespace

Outcome<PreprocessResult> preprocess(ImageView view, const PreprocessConfig& config) noexcept {
  try {
    const auto validated = validateImage(view);
    if (!validated.hasValue()) {
      return Outcome<PreprocessResult>::failure(*validated.error());
    }

    const ImageView image = *validated.value();
    const cv::Mat source = viewToMat(image);
    const ResizeTransform transform =
        computeResizeTransform(image.width, image.height, config.targetMaxDimension);
    const cv::Mat resizedColor = resizeMat(source, transform);
    cv::Mat gray = toGray(resizedColor, image.format);
    if (!gray.isContinuous()) {
      gray = gray.clone();
    }

    Photometry photometry = measurePhotometry(gray);
    if (photometry.luminanceStdDev < config.minProcessLuminanceStdDev) {
      return preprocessFailure(ErrorCode::InsufficientContrast, "luminance stddev");
    }
    if (photometry.meanLuminance < config.processMeanLuminanceMin ||
        photometry.meanLuminance > config.processMeanLuminanceMax) {
      return preprocessFailure(ErrorCode::InsufficientLighting, "mean luminance");
    }

    photometry.acceptableContrast =
        photometry.luminanceStdDev >= config.minAcceptableLuminanceStdDev;
    photometry.acceptableLighting =
        photometry.meanLuminance >= config.acceptableMeanLuminanceMin &&
        photometry.meanLuminance <= config.acceptableMeanLuminanceMax;

    cv::Mat enhanced = gray;
    if (config.normalizeIllumination) {
      enhanced = normalizeIllumination(enhanced);
    }
    enhanced = applyBlur(enhanced, config.blurKernel);
    const cv::Mat binary = binarize(enhanced, config);

    PreprocessResult result{};
    result.gray = std::move(enhanced);
    result.binary = binary;
    result.transform = transform;
    result.photometry = photometry;
    return Outcome<PreprocessResult>::success(std::move(result));
  } catch (const cv::Exception&) {
    return preprocessFailure(ErrorCode::PreprocessFailed, "opencv");
  } catch (const std::bad_alloc&) {
    return Outcome<PreprocessResult>::failure(
        {ErrorCode::InternalError, Stage::Preprocess, "allocation"});
  } catch (...) {
    return Outcome<PreprocessResult>::failure(
        {ErrorCode::InternalError, Stage::Preprocess, "unknown"});
  }
}

}  // namespace leaf::detail
