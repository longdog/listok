#include <leaf/config.h>

#include <cmath>

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

bool validatePreprocess(const PreprocessConfig& preprocess) noexcept {
  if (!inClosedRange(preprocess.targetMaxDimension, 256, 8192)) {
    return false;
  }
  if (!inClosedRange(preprocess.blurKernel, 1, 31) || !isOddInt(preprocess.blurKernel)) {
    return false;
  }
  if (!inClosedRange(preprocess.adaptiveThresholdBlockSize, 3, 255) ||
      !isOddInt(preprocess.adaptiveThresholdBlockSize)) {
    return false;
  }
  if (!isFinite(preprocess.adaptiveThresholdC) ||
      !inClosedRange(preprocess.adaptiveThresholdC, -64.0, 64.0)) {
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

bool validateScoreRanges(const std::array<ScoreRange, 5>& ranges, Error& error) noexcept {
  for (std::size_t index = 0; index < ranges.size(); ++index) {
    const ScoreRange& range = ranges[index];
    const int expectedScore = static_cast<int>(index) + 1;
    if (range.score != expectedScore || !isFinite(range.min)) {
      error = {ErrorCode::InvalidScoreRanges, Stage::Score, "score identity"};
      return false;
    }
    if (index == 0) {
      if (range.min != 0.0) {
        error = {ErrorCode::InvalidScoreRanges, Stage::Score, "first min"};
        return false;
      }
    } else if (range.min != ranges[index - 1].max) {
      error = {ErrorCode::InvalidScoreRanges, Stage::Score, "contiguity"};
      return false;
    }

    const bool isLast = index == ranges.size() - 1;
    if (isLast) {
      if (range.max.has_value()) {
        error = {ErrorCode::InvalidScoreRanges, Stage::Score, "open top"};
        return false;
      }
      continue;
    }

    if (!range.max.has_value() || !isFinite(*range.max) || *range.max <= range.min) {
      error = {ErrorCode::InvalidScoreRanges, Stage::Score, "half-open range"};
      return false;
    }
  }
  return true;
}

}  // namespace

bool isValidPreprocessConfig(const PreprocessConfig& preprocess) noexcept {
  return validatePreprocess(preprocess);
}

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

  Error scoreError;
  if (!validateScoreRanges(value.scoreRanges, scoreError)) {
    return Outcome<AnalyzerConfig>::failure(scoreError);
  }

  return Outcome<AnalyzerConfig>::success(std::move(value));
}

}  // namespace leaf
