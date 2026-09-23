#pragma once

#include <leaf/outcome.h>
#include <leaf/types.h>

#include <cstdint>
#include <string>
#include <vector>

struct MeasurementDomain {
  leaf::LeafContour contour;
  leaf::CenterVein center;
  leaf::SecondaryVeins veins;
  leaf::LeafKeypoints keypoints;
  leaf::CoordinateSystem cs;
};

struct SyntheticLeafFixture {
  std::int32_t width{};
  std::int32_t height{};
  std::vector<std::uint8_t> rgb;
  std::vector<std::uint8_t> mask;
  std::string groundTruthPath;
};

struct GroundTruth {
  std::string algorithmVersion;
  leaf::Path contour;
  leaf::Path center;
  leaf::Point a1, a2, b1, b2, c1, c2, d1, d2, e, f, g1, g2;
  leaf::Path leftFirst;
  leaf::Path leftSecond;
  leaf::Path rightFirst;
  leaf::Path rightSecond;
  leaf::SideMeasurements left;
  leaf::SideMeasurements right;
  leaf::RelativeAsymmetry asymmetry;
  double k{};
  int score{};
};

MeasurementDomain canonicalMeasurementDomain();
SyntheticLeafFixture makeSyntheticLeafV1();
leaf::Outcome<GroundTruth> readGroundTruth(const std::string& path);
