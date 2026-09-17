#pragma once

#include <leaf/outcome.h>

#include <array>
#include <optional>

namespace leaf {

struct ScoreRange {
  double min{};
  std::optional<double> max;
  int score{};
};

struct PreprocessConfig {
  int targetMaxDimension{2048};
  bool normalizeIllumination{true};
  int blurKernel{5};
  bool adaptiveThreshold{false};
  double minProcessLuminanceStdDev{8};
  double minAcceptableLuminanceStdDev{12};
  double processMeanLuminanceMin{10};
  double processMeanLuminanceMax{245};
  double acceptableMeanLuminanceMin{20};
  double acceptableMeanLuminanceMax{235};
};

struct DetectionConfig {
  double minLeafAreaRatio{.05};
  double maxLeafAreaRatio{.95};
  int frameMarginPx{2};
  double minContourSolidity{.80};
  int maxContourCandidates{8};
  double minCenterVeinLengthRatio{.55};
  int maxCenterVeinGapPx{12};
  int veinThresholdBlockSize{31};
  double veinThresholdC{5};
  double minSkeletonBranchLengthNorm{.04};
  double minSecondaryVeinLengthNorm{.08};
  double minSecondaryAngleDeg{15};
  double maxSecondaryAngleDeg{165};
  double minAttachmentSeparationNorm{.05};
  double endpointContourSnapPx{8};
  double branchMergeRadiusPx{4};
  double ambiguityScoreDelta{.03};
  double minimumStageConfidence{.50};
};

struct MeasurementConfig {
  double minDenominator{1e-9};
};

struct QualityConfig {
  double minimumLeafConfidence{.50};
  double minimumCenterVeinConfidence{.50};
  double minimumSecondaryVeinConfidence{.50};
  double minimumKeypointConfidence{.50};
  double minimumMeasurementConfidence{.50};
};

struct AnalyzerConfig {
  PreprocessConfig preprocess;
  DetectionConfig detection;
  MeasurementConfig measurement;
  QualityConfig quality;
  std::array<ScoreRange, 5> scoreRanges{};
};

AnalyzerConfig defaultAnalyzerConfig() noexcept;
Outcome<AnalyzerConfig> validateConfig(AnalyzerConfig value) noexcept;

}  // namespace leaf
