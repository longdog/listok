#pragma once

#include <leaf/outcome.h>
#include <leaf/types.h>

#include <cstddef>
#include <optional>
#include <vector>

namespace leaf {

struct AnalysisResult {
  Versions versions;
  CoordinateSystem coordinateSystem;
  LeafContour leaf;
  CenterVein centerVein;
  SecondaryVeins secondaryVeins;
  LeafKeypoints keypoints;
  LeafMeasurements measurements;
  LeafAsymmetry asymmetry;
  int score{};
  QualityAssessment quality;
};

struct BatchItem {
  Outcome<AnalysisResult> outcome;
};

struct BatchResult {
  Versions versions;
  std::vector<BatchItem> items;
  std::size_t total{};
  std::size_t successful{};
  std::size_t acceptable{};
  std::optional<double> meanAsymmetry;
  bool valid{};
  int score{};
};

}  // namespace leaf
