#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace leaf {

struct Point {
  double x{0};
  double y{0};
};

struct Rect {
  double x{0};
  double y{0};
  double width{0};
  double height{0};
};

using Path = std::vector<Point>;

struct CoordinateSystem {
  Point origin{};
  Point xAxis{1, 0};
  Point yAxis{0, 1};
  double scale{1};

  Point imageToNormalized(Point p) const noexcept;
  Point normalizedToImage(Point p) const noexcept;
};

struct NormalizedPoint {
  Point image{};
  Point normalized{};
};

struct Versions {
  std::string resultSchemaVersion{"1.0"};
  std::string libraryVersion;
  std::string algorithmVersion;
  std::string methodologyVersion;
  std::optional<std::string> modelVersion{};
};

Versions defaultVersions() noexcept;
const char* libraryVersion() noexcept;
const char* algorithmVersion() noexcept;
const char* methodologyVersion() noexcept;

struct LeafContour {
  Path path;
  double area{};
  Rect boundingBox{};
  double confidence{};
};

struct CenterVein {
  Path path;
  Point base{};
  Point apex{};
  double confidence{};
};

struct Vein {
  Path path;
  Point attachment{};
  Point endpoint{};
  double arcLength{};
  double confidence{};
};

struct SecondaryVeins {
  Vein leftFirst;
  Vein leftSecond;
  Vein rightFirst;
  Vein rightSecond;
  double confidence{};
};

struct LeafKeypoints {
  NormalizedPoint a1, a2, b1, b2, c1, c2, d1, d2, e, f, g1, g2;
  double confidence{};
};

struct SideMeasurements {
  double m1{};
  double m2{};
  double m3{};
  double m4{};
  double m5{};
};

struct LeafMeasurements {
  SideMeasurements left;
  SideMeasurements right;
  double confidence{};
};

struct RelativeAsymmetry {
  double m1{};
  double m2{};
  double m3{};
  double m4{};
  double m5{};
};

struct LeafAsymmetry {
  RelativeAsymmetry features;
  double value{};
};

enum class QualityIssue {
  LowLeafConfidence,
  LowCenterVeinConfidence,
  LowSecondaryVeinConfidence,
  LowKeypointConfidence,
  LowMeasurementConfidence,
  MarginalContrast,
  MarginalLighting,
};

struct QualityAssessment {
  bool acceptable{};
  double overallConfidence{};
  double leafConfidence{};
  double centerVeinConfidence{};
  double secondaryVeinConfidence{};
  double keypointConfidence{};
  double measurementConfidence{};
  bool sufficientContrast{};
  bool sufficientLighting{};
  std::vector<QualityIssue> issues;
};

}  // namespace leaf
