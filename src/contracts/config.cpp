#include <leaf/analyzer.h>
#include <leaf/config.h>

#include <cmath>
#include <limits>
#include <memory>
#include <utility>

namespace leaf {
namespace {

bool isFinite(double value) noexcept {
  return std::isfinite(value);
}

bool isOddInt(int value) noexcept { return (value % 2) != 0; }

bool inClosedRange(double value, double minValue, double maxValue) noexcept {
  return value >= minValue && value <= maxValue;
}

bool inOpenClosedRange(double value, double minValue, double maxValue) noexcept {
  return value > minValue && value <= maxValue;
}

bool inOpenRange(double value, double minValue, double maxValue) noexcept {
  return value > minValue && value < maxValue;
}

Outcome<AnalyzerConfig> configFailure(const std::string& message) noexcept {
  return Outcome<AnalyzerConfig>::failure(
      {ErrorCode::InvalidConfigValue, Stage::Config, message});
}

Outcome<AnalyzerConfig> scoreFailure(const std::string& message) noexcept {
  return Outcome<AnalyzerConfig>::failure(
      {ErrorCode::InvalidScoreRanges, Stage::Score, message});
}

bool validatePreprocess(const PreprocessConfig& preprocess) noexcept {
  if (!inClosedRange(preprocess.targetMaxDimension, 256, 8192)) {
    return false;
  }
  if (!inClosedRange(preprocess.blurKernel, 1, 31) || !isOddInt(preprocess.blurKernel)) {
    return false;
  }
  if (!isFinite(preprocess.minProcessLuminanceStdDev) ||
      !isFinite(preprocess.minAcceptableLuminanceStdDev) ||
      preprocess.minProcessLuminanceStdDev <= 0.0 ||
      preprocess.minAcceptableLuminanceStdDev <= 0.0 ||
      preprocess.minProcessLuminanceStdDev > preprocess.minAcceptableLuminanceStdDev) {
    return false;
  }
  if (!isFinite(preprocess.processMeanLuminanceMin) ||
      !isFinite(preprocess.processMeanLuminanceMax) ||
      !isFinite(preprocess.acceptableMeanLuminanceMin) ||
      !isFinite(preprocess.acceptableMeanLuminanceMax) ||
      preprocess.processMeanLuminanceMin > preprocess.processMeanLuminanceMax ||
      preprocess.acceptableMeanLuminanceMin > preprocess.acceptableMeanLuminanceMax ||
      preprocess.processMeanLuminanceMin > preprocess.acceptableMeanLuminanceMin ||
      preprocess.processMeanLuminanceMax < preprocess.acceptableMeanLuminanceMax) {
    return false;
  }
  return true;
}

bool validateDetection(const DetectionConfig& detection) noexcept {
  if (!inOpenRange(detection.minLeafAreaRatio, 0.0, 1.0) ||
      !inOpenClosedRange(detection.maxLeafAreaRatio, 0.0, 1.0) ||
      detection.minLeafAreaRatio >= detection.maxLeafAreaRatio) {
    return false;
  }
  if (!inClosedRange(detection.frameMarginPx, 0, 64) ||
      !inClosedRange(detection.minContourSolidity, 0.0, 1.0) ||
      !inClosedRange(detection.maxContourCandidates, 1, 64) ||
      !inOpenClosedRange(detection.minCenterVeinLengthRatio, 0.0, 1.0) ||
      !inClosedRange(detection.maxCenterVeinGapPx, 0, 128)) {
    return false;
  }
  if (!inClosedRange(detection.veinThresholdBlockSize, 3, 255) ||
      !isOddInt(detection.veinThresholdBlockSize)) {
    return false;
  }
  if (!inClosedRange(detection.veinThresholdC, -64.0, 64.0) ||
      !inOpenClosedRange(detection.minSkeletonBranchLengthNorm, 0.0, 1.0) ||
      !inOpenClosedRange(detection.minSecondaryVeinLengthNorm, 0.0, 1.0) ||
      !inClosedRange(detection.minSecondaryAngleDeg, 0.0, 180.0) ||
      !inClosedRange(detection.maxSecondaryAngleDeg, 0.0, 180.0) ||
      detection.minSecondaryAngleDeg >= detection.maxSecondaryAngleDeg ||
      detection.minSkeletonBranchLengthNorm > detection.minSecondaryVeinLengthNorm ||
      !inOpenClosedRange(detection.minAttachmentSeparationNorm, 0.0, 1.0) ||
      !inClosedRange(detection.endpointContourSnapPx, 0.0, 64.0) ||
      !inClosedRange(detection.branchMergeRadiusPx, 0.0, 32.0) ||
      !inClosedRange(detection.ambiguityScoreDelta, 0.0, 1.0) ||
      !inClosedRange(detection.minimumStageConfidence, 0.0, 1.0)) {
    return false;
  }
  return true;
}

bool validateMeasurement(const MeasurementConfig& measurement) noexcept {
  return isFinite(measurement.minDenominator) && measurement.minDenominator > 0.0;
}

bool validateQuality(const QualityConfig& quality, double minimumStageConfidence) noexcept {
  const double thresholds[] = {
      quality.minimumLeafConfidence,
      quality.minimumCenterVeinConfidence,
      quality.minimumSecondaryVeinConfidence,
      quality.minimumKeypointConfidence,
      quality.minimumMeasurementConfidence,
  };
  for (const double threshold : thresholds) {
    if (!inClosedRange(threshold, 0.0, 1.0) || threshold < minimumStageConfidence) {
      return false;
    }
  }
  return true;
}

Outcome<AnalyzerConfig> validateScoreRanges(const std::array<ScoreRange, 5>& ranges) noexcept {
  for (std::size_t index = 0; index < ranges.size(); ++index) {
    const ScoreRange& range = ranges[index];
    const int expectedScore = static_cast<int>(index) + 1;
    if (range.score != expectedScore || !isFinite(range.min)) {
      return scoreFailure("score identity");
    }
    if (index == 0) {
      if (range.min != 0.0) {
        return scoreFailure("first min");
      }
    } else if (range.min != ranges[index - 1].max) {
      return scoreFailure("contiguity");
    }

    const bool isLast = index == ranges.size() - 1;
    if (isLast) {
      if (range.max.has_value()) {
        return scoreFailure("open top");
      }
      continue;
    }

    if (!range.max.has_value() || !isFinite(*range.max) || *range.max <= range.min) {
      return scoreFailure("half-open range");
    }
  }
  return Outcome<AnalyzerConfig>::success(AnalyzerConfig{});
}

}  // namespace

Outcome<AnalyzerConfig> validateConfig(AnalyzerConfig value) noexcept {
  if (!validatePreprocess(value.preprocess)) {
    return configFailure("preprocess");
  }
  if (!validateDetection(value.detection)) {
    return configFailure("detection");
  }
  if (!validateMeasurement(value.measurement)) {
    return configFailure("measurement");
  }
  if (!validateQuality(value.quality, value.detection.minimumStageConfidence)) {
    return configFailure("quality");
  }

  const auto scoreValidation = validateScoreRanges(value.scoreRanges);
  if (!scoreValidation.hasValue()) {
    return scoreValidation;
  }

  return Outcome<AnalyzerConfig>::success(std::move(value));
}

class Analyzer::Impl {};

Analyzer::Analyzer(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

Analyzer::~Analyzer() = default;

Analyzer::Analyzer(Analyzer&&) noexcept = default;

Analyzer& Analyzer::operator=(Analyzer&&) noexcept = default;

Outcome<std::unique_ptr<Analyzer>> Analyzer::create(AnalyzerConfig config) noexcept {
  const auto validated = validateConfig(std::move(config));
  if (!validated.hasValue()) {
    return Outcome<std::unique_ptr<Analyzer>>::failure(*validated.error());
  }
  return Outcome<std::unique_ptr<Analyzer>>::success(
      std::unique_ptr<Analyzer>(new Analyzer(std::make_unique<Impl>())));
}

Outcome<AnalysisResult> Analyzer::analyze(ImageView image) const noexcept {
  const auto validatedImage = validateImage(image);
  if (!validatedImage.hasValue()) {
    return Outcome<AnalysisResult>::failure(*validatedImage.error());
  }
  return Outcome<AnalysisResult>::failure(
      {ErrorCode::InternalError, Stage::Internal, "analyzer pipeline is not implemented"});
}

BatchResult Analyzer::analyzeBatch(std::span<const ImageView> images) const noexcept {
  BatchResult batch;
  batch.versions = defaultVersions();
  batch.total = images.size();
  batch.valid = false;
  batch.score = 0;
  for (const ImageView& image : images) {
    batch.items.push_back(BatchItem{analyze(image)});
    if (batch.items.back().outcome.hasValue()) {
      ++batch.successful;
    }
  }
  return batch;
}

}  // namespace leaf
