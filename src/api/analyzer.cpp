#include <leaf/analyzer.h>

#include <leaf/config.h>
#include <leaf/image.h>
#include <leaf/types.h>

#include "../detection/center/center_vein_detector.h"
#include "../detection/contour/opencv_leaf_detector.h"
#include "../detection/secondary/keypoint_extractor.h"
#include "../detection/secondary/secondary_vein_detector.h"
#include "../geometry/coordinate_system.h"
#include "../measurement/extractor.h"
#include "../preprocess/preprocessor.h"
#include "../skeleton/graph.h"
#include "../skeleton/thinning.h"
#include "../statistics/asymmetry.h"
#include "../statistics/batch.h"
#include "../statistics/score.h"
#include "../validation/quality.h"

#include <opencv2/core.hpp>
#include <opencv2/core/ocl.hpp>

#include <cmath>
#include <memory>
#include <mutex>
#include <utility>

namespace leaf {

class Analyzer::Impl {
 public:
  explicit Impl(AnalyzerConfig config) : config(std::move(config)) {}

  AnalyzerConfig config;
};

namespace {

std::once_flag cvSetup;

void setupOpenCv() noexcept {
  std::call_once(cvSetup, [] {
    cv::ocl::setUseOpenCL(false);
    cv::setNumThreads(1);
  });
}

Outcome<double> confidence(double value, Stage stage) noexcept {
  if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
    return Outcome<double>::failure({ErrorCode::InternalError, stage, "invalid stage confidence"});
  }
  return Outcome<double>::success(value);
}

}  // namespace

Analyzer::Analyzer(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

Analyzer::~Analyzer() = default;

Analyzer::Analyzer(Analyzer&&) noexcept = default;

Analyzer& Analyzer::operator=(Analyzer&&) noexcept = default;

Outcome<std::unique_ptr<Analyzer>> Analyzer::create(AnalyzerConfig config) noexcept {
  try {
    const auto validated = validateConfig(std::move(config));
    if (!validated.hasValue()) {
      return Outcome<std::unique_ptr<Analyzer>>::failure(*validated.error());
    }
    setupOpenCv();
    return Outcome<std::unique_ptr<Analyzer>>::success(std::unique_ptr<Analyzer>(
        new Analyzer(std::make_unique<Impl>(*validated.value()))));
  } catch (const std::bad_alloc&) {
    return Outcome<std::unique_ptr<Analyzer>>::failure(
        {ErrorCode::InternalError, Stage::Internal, "out of memory"});
  } catch (...) {
    return Outcome<std::unique_ptr<Analyzer>>::failure(
        {ErrorCode::InternalError, Stage::Internal, "analyzer create failed"});
  }
}

Outcome<AnalysisResult> Analyzer::analyze(ImageView image) const noexcept {
  try {
    setupOpenCv();
    const auto validatedImage = validateImage(image);
    if (!validatedImage.hasValue()) {
      return Outcome<AnalysisResult>::failure(*validatedImage.error());
    }

    const auto& config = impl_->config;
    const auto prepared = detail::preprocess(image, config.preprocess);
    if (!prepared.hasValue()) return Outcome<AnalysisResult>::failure(*prepared.error());
    const auto contour = detail::detectLeafContour(*prepared.value(), config.detection);
    if (!contour.hasValue()) return Outcome<AnalysisResult>::failure(*contour.error());
    if (!confidence(contour.value()->confidence, Stage::LeafContour).hasValue()) {
      return Outcome<AnalysisResult>::failure(*confidence(contour.value()->confidence, Stage::LeafContour).error());
    }
    const auto center = detail::detectCenterVein(*prepared.value(), *contour.value(), config.detection);
    if (!center.hasValue()) return Outcome<AnalysisResult>::failure(*center.error());
    if (!confidence(center.value()->confidence, Stage::CenterVein).hasValue()) {
      return Outcome<AnalysisResult>::failure(*confidence(center.value()->confidence, Stage::CenterVein).error());
    }
    const auto coordinates = detail::makeCoordinateSystem(*center.value());
    if (!coordinates.hasValue()) return Outcome<AnalysisResult>::failure(*coordinates.error());
    const auto skeleton = detail::zhangSuen(prepared.value()->binary);
    if (!skeleton.hasValue()) return Outcome<AnalysisResult>::failure(*skeleton.error());
    cv::Mat veinSkeleton = *skeleton.value();
    detail::cleanupVeinSkeleton(veinSkeleton, *contour.value());
    const auto graph = detail::buildSkeletonGraph(veinSkeleton);
    if (!graph.hasValue()) return Outcome<AnalysisResult>::failure(*graph.error());
    const auto pruned = detail::pruneAndMerge(*graph.value(), *center.value(), config.detection);
    if (!pruned.hasValue()) return Outcome<AnalysisResult>::failure(*pruned.error());
    const auto secondary = detail::detectSecondaryVeins(
        *pruned.value(), *contour.value(), *center.value(), *coordinates.value(), config.detection);
    if (!secondary.hasValue()) return Outcome<AnalysisResult>::failure(*secondary.error());
    if (!confidence(secondary.value()->confidence, Stage::SecondaryVeins).hasValue()) {
      return Outcome<AnalysisResult>::failure(
          *confidence(secondary.value()->confidence, Stage::SecondaryVeins).error());
    }
    const auto keypoints = detail::extractKeypoints(
        *contour.value(), *center.value(), *secondary.value(), *coordinates.value());
    if (!keypoints.hasValue()) return Outcome<AnalysisResult>::failure(*keypoints.error());
    if (!confidence(keypoints.value()->confidence, Stage::Keypoints).hasValue()) {
      return Outcome<AnalysisResult>::failure(*confidence(keypoints.value()->confidence, Stage::Keypoints).error());
    }
    const auto measurements = detail::extractMeasurements(
        *contour.value(), *center.value(), *secondary.value(), *keypoints.value(), *coordinates.value());
    if (!measurements.hasValue()) return Outcome<AnalysisResult>::failure(*measurements.error());
    if (!confidence(measurements.value()->confidence, Stage::Measurements).hasValue()) {
      return Outcome<AnalysisResult>::failure(
          *confidence(measurements.value()->confidence, Stage::Measurements).error());
    }
    const auto asymmetry = detail::calculateAsymmetry(*measurements.value(), config.measurement.minDenominator);
    if (!asymmetry.hasValue()) return Outcome<AnalysisResult>::failure(*asymmetry.error());
    const auto score = detail::calculateScore(asymmetry.value()->value, config.scoreRanges);
    if (!score.hasValue()) return Outcome<AnalysisResult>::failure(*score.error());
    const auto quality = detail::assessQuality(
        prepared.value()->photometry, contour.value()->confidence, center.value()->confidence,
        secondary.value()->confidence, keypoints.value()->confidence, measurements.value()->confidence,
        config.quality);
    if (!quality.hasValue()) return Outcome<AnalysisResult>::failure(*quality.error());

    AnalysisResult result;
    result.versions = defaultVersions();
    result.coordinateSystem = *coordinates.value();
    result.leaf = *contour.value();
    result.centerVein = *center.value();
    result.secondaryVeins = *secondary.value();
    result.keypoints = *keypoints.value();
    result.measurements = *measurements.value();
    result.asymmetry = *asymmetry.value();
    result.score = *score.value();
    result.quality = *quality.value();
    return Outcome<AnalysisResult>::success(std::move(result));
  } catch (const std::bad_alloc&) {
    return Outcome<AnalysisResult>::failure(
        {ErrorCode::InternalError, Stage::Internal, "out of memory"});
  } catch (...) {
    return Outcome<AnalysisResult>::failure(
        {ErrorCode::InternalError, Stage::Internal, "analyzer analyze failed"});
  }
}

BatchResult Analyzer::analyzeBatch(std::span<const ImageView> images) const noexcept {
  try {
    std::vector<BatchItem> items;
    items.reserve(images.size());
    for (const ImageView& image : images) {
      items.push_back(BatchItem{analyze(image)});
    }
    return detail::aggregateBatch(std::move(items), defaultVersions(), impl_->config.scoreRanges);
  } catch (const std::bad_alloc&) {
    return BatchResult{defaultVersions(), {}, images.size(), 0, 0, std::nullopt, false, 0};
  } catch (...) {
    return BatchResult{defaultVersions(), {}, images.size(), 0, 0, std::nullopt, false, 0};
  }
}

}  // namespace leaf
