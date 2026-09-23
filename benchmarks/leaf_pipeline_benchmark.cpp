#include <leaf/analyzer.h>
#include <leaf/config.h>

#include "center_vein_detector.h"
#include "coordinate_system.h"
#include "extractor.h"
#include "graph.h"
#include "keypoint_extractor.h"
#include "opencv_leaf_detector.h"
#include "preprocessor.h"
#include "secondary_vein_detector.h"
#include "thinning.h"

#include <opencv2/core/version.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr int kWidth = 4000;
constexpr int kHeight = 3000;

struct StageSample {
  double preprocessMs = 0;
  double contourMs = 0;
  double centerMs = 0;
  double skeletonMs = 0;
  double measurementsMs = 0;
  double totalMs = 0;
};

double milliseconds(std::chrono::steady_clock::duration duration) {
  return std::chrono::duration<double, std::milli>(duration).count();
}

double percentile(std::vector<double> values, double fraction) {
  if (values.empty()) {
    return 0;
  }
  std::sort(values.begin(), values.end());
  const std::size_t index = static_cast<std::size_t>(
      std::min(values.size() - 1,
               static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1) + 0.5)));
  return values[index];
}

std::string cpuModel() {
  std::ifstream cpuinfo("/proc/cpuinfo");
  std::string line;
  while (std::getline(cpuinfo, line)) {
    const auto pos = line.find("model name");
    if (pos != std::string::npos) {
      const auto colon = line.find(':');
      if (colon != std::string::npos) {
        return line.substr(colon + 2);
      }
    }
  }
  return "unknown";
}

std::vector<std::uint8_t> loadRgb(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kWidth) * kHeight * 3u);
  input.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
  if (!input) {
    pixels.clear();
  }
  return pixels;
}

StageSample measureOnce(const leaf::Analyzer& analyzer, leaf::ImageView view,
                        const leaf::AnalyzerConfig& config) {
  StageSample sample;
  const auto totalStart = std::chrono::steady_clock::now();
  (void)analyzer.analyze(view);
  sample.totalMs = milliseconds(std::chrono::steady_clock::now() - totalStart);

  auto mark = [](auto start) {
    return milliseconds(std::chrono::steady_clock::now() - start);
  };
  const auto preprocessStart = std::chrono::steady_clock::now();
  const auto prepared = leaf::detail::preprocess(view, config.preprocess);
  sample.preprocessMs = mark(preprocessStart);
  if (!prepared.hasValue()) {
    return sample;
  }
  const auto contourStart = std::chrono::steady_clock::now();
  const auto contour = leaf::detail::detectLeafContour(*prepared.value(), config.detection);
  sample.contourMs = mark(contourStart);
  if (!contour.hasValue()) {
    return sample;
  }
  const auto centerStart = std::chrono::steady_clock::now();
  const auto center =
      leaf::detail::detectCenterVein(*prepared.value(), *contour.value(), config.detection);
  sample.centerMs = mark(centerStart);
  if (!center.hasValue()) {
    return sample;
  }
  const auto skeletonStart = std::chrono::steady_clock::now();
  const auto coordinates = leaf::detail::makeCoordinateSystem(*center.value());
  const auto thinned = leaf::detail::zhangSuen(prepared.value()->binary);
  if (thinned.hasValue() && coordinates.hasValue()) {
    const auto graph = leaf::detail::buildSkeletonGraph(*thinned.value());
    if (graph.hasValue()) {
      const auto pruned =
          leaf::detail::pruneAndMerge(*graph.value(), *center.value(), config.detection);
      if (pruned.hasValue()) {
        (void)leaf::detail::detectSecondaryVeins(*pruned.value(), *contour.value(), *center.value(),
                                                 *coordinates.value(), config.detection);
      }
    }
  }
  sample.skeletonMs = mark(skeletonStart);
  const auto measureStart = std::chrono::steady_clock::now();
  if (coordinates.hasValue() && thinned.hasValue()) {
    const auto skeletonGraph = leaf::detail::buildSkeletonGraph(*thinned.value());
    if (skeletonGraph.hasValue()) {
      const auto pruned =
          leaf::detail::pruneAndMerge(*skeletonGraph.value(), *center.value(), config.detection);
      if (pruned.hasValue()) {
        const auto veins = leaf::detail::detectSecondaryVeins(
            *pruned.value(), *contour.value(), *center.value(), *coordinates.value(), config.detection);
        if (veins.hasValue()) {
          const auto keypoints = leaf::detail::extractKeypoints(
              *contour.value(), *center.value(), *veins.value(), *coordinates.value());
          if (keypoints.hasValue()) {
            (void)leaf::detail::extractMeasurements(*contour.value(), *center.value(), *veins.value(),
                                                   *keypoints.value(), *coordinates.value());
          }
        }
      }
    }
  }
  sample.measurementsMs = mark(measureStart);
  return sample;
}

void printStat(const char* name, const std::vector<double>& values) {
  std::cout << name << " median_ms=" << percentile(values, 0.50)
            << " p95_ms=" << percentile(values, 0.95) << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  int warmup = 5;
  int iterations = 30;
  std::string input = "tests/fixtures/benchmark/leaf_12mp.rgb";
  for (int i = 1; i < argc; ++i) {
    const std::string flag = argv[i];
    if (flag == "--warmup" && i + 1 < argc) warmup = std::stoi(argv[++i]);
    else if (flag == "--iterations" && i + 1 < argc) iterations = std::stoi(argv[++i]);
    else if (flag == "--input" && i + 1 < argc) input = argv[++i];
  }

  auto pixels = loadRgb(input);
  if (pixels.size() != static_cast<std::size_t>(kWidth) * kHeight * 3u) {
    std::cerr << "expected decoded " << kWidth << "x" << kHeight << " RGB8 at " << input << "\n";
    return 1;
  }
  const leaf::ImageView view{pixels.data(), kWidth, kHeight, kWidth * 3, leaf::PixelFormat::RGB8};
  const auto config = leaf::defaultAnalyzerConfig();
  auto analyzer = leaf::Analyzer::create(config);
  if (!analyzer.hasValue()) {
    std::cerr << "analyzer create failed\n";
    return 1;
  }

  for (int i = 0; i < warmup; ++i) {
    (void)analyzer.value()->get()->analyze(view);
  }

  std::vector<double> preprocess, contour, center, skeleton, measurements, total;
  for (int i = 0; i < iterations; ++i) {
    const StageSample sample = measureOnce(**analyzer.value(), view, config);
    preprocess.push_back(sample.preprocessMs);
    contour.push_back(sample.contourMs);
    center.push_back(sample.centerMs);
    skeleton.push_back(sample.skeletonMs);
    measurements.push_back(sample.measurementsMs);
    total.push_back(sample.totalMs);
  }

  std::cout << "machine cpu=" << cpuModel() << "\n";
  std::cout << "compiler=" << __VERSION__ << "\n";
  std::cout << "opencv=" << CV_VERSION << "\n";
  std::cout << "image=" << kWidth << "x" << kHeight << " RGB8 warmup=" << warmup
            << " iterations=" << iterations << "\n";
  printStat("preprocess", preprocess);
  printStat("contour", contour);
  printStat("center_vein", center);
  printStat("skeleton_secondary", skeleton);
  printStat("measurements", measurements);
  printStat("total", total);
  return 0;
}
