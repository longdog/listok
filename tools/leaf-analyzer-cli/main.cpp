#include "image_io.h"

#include <leaf/analyzer.h>
#include <leaf/json.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(LEAF_ENABLE_DEBUG)
#include "debug_renderer.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#endif

namespace {

struct Arguments {
  const char* input = nullptr;
  const char* output = nullptr;
  const char* config = nullptr;
  const char* debug = nullptr;
  const char* inputDir = nullptr;
  const char* outputDir = nullptr;
  const char* debugDir = nullptr;
};

void usage(std::ostream& out) {
  out << "leaf-analyzer --input <image> --output <json> [--config <json>] [--debug <dir>]\n"
      << "leaf-analyzer --input-dir <dir> --output-dir <dir> [--config <json>] [--debug-dir <dir>]\n"
      << "EXIF orientation is not applied; inputs must be canonically oriented.\n";
}

bool takeValue(int argc, char** argv, int& index, const char*& slot) {
  if (slot != nullptr || index + 1 >= argc || std::string_view(argv[index + 1]).starts_with("--")) {
    return false;
  }
  slot = argv[++index];
  return true;
}

int parseArguments(int argc, char** argv, Arguments& args) {
  bool sawDebugFlag = false;
  for (int index = 1; index < argc; ++index) {
    const std::string_view flag(argv[index]);
    if (flag == "--input") {
      if (!takeValue(argc, argv, index, args.input)) return 2;
    } else if (flag == "--output") {
      if (!takeValue(argc, argv, index, args.output)) return 2;
    } else if (flag == "--config") {
      if (!takeValue(argc, argv, index, args.config)) return 2;
    } else if (flag == "--debug") {
      sawDebugFlag = true;
      if (!takeValue(argc, argv, index, args.debug)) return 2;
    } else if (flag == "--input-dir") {
      if (!takeValue(argc, argv, index, args.inputDir)) return 2;
    } else if (flag == "--output-dir") {
      if (!takeValue(argc, argv, index, args.outputDir)) return 2;
    } else if (flag == "--debug-dir") {
      sawDebugFlag = true;
      if (!takeValue(argc, argv, index, args.debugDir)) return 2;
    } else if (flag == "--help") {
      usage(std::cout);
      return 0;
    } else {
      std::cerr << "unknown option: " << flag << "\n";
      usage(std::cerr);
      return 2;
    }
  }

#if !defined(LEAF_ENABLE_DEBUG)
  if (sawDebugFlag) {
    std::cerr << "debug support is unavailable in this build\n";
    return 2;
  }
#else
  (void)sawDebugFlag;
#endif

  const bool single = args.input || args.output || args.debug;
  const bool batch = args.inputDir || args.outputDir || args.debugDir;
  if (single == batch) {
    std::cerr << "choose either single mode or batch mode\n";
    usage(std::cerr);
    return 2;
  }
  if (single && (args.input == nullptr || args.output == nullptr)) {
    std::cerr << "single mode requires --input and --output\n";
    return 2;
  }
  if (batch && (args.inputDir == nullptr || args.outputDir == nullptr)) {
    std::cerr << "batch mode requires --input-dir and --output-dir\n";
    return 2;
  }
  return -1;
}

bool writeAtomic(const std::filesystem::path& destination, std::string_view bytes) {
  const std::filesystem::path temporary = destination.string() + ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      return false;
    }
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output) {
      output.close();
      std::filesystem::remove(temporary);
      return false;
    }
  }
  std::error_code error;
  std::filesystem::rename(temporary, destination, error);
  if (error) {
    std::filesystem::remove(temporary);
    return false;
  }
  return true;
}

int writeError(const std::filesystem::path& destination, const leaf::Error& error) {
  const auto json = leaf::errorToJson(error, leaf::defaultVersions());
  if (!json.hasValue()) {
    return 5;
  }
  return writeAtomic(destination, *json.value()) ? 4 : 5;
}

leaf::Outcome<leaf::AnalyzerConfig> loadConfig(const char* path) {
  if (path == nullptr) {
    return leaf::Outcome<leaf::AnalyzerConfig>::success(leaf::defaultAnalyzerConfig());
  }
  std::ifstream input(path);
  if (!input) {
    return leaf::Outcome<leaf::AnalyzerConfig>::failure(
        {leaf::ErrorCode::InvalidConfigJson, leaf::Stage::Config, "unreadable config"});
  }
  const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  return leaf::parseAnalyzerConfigJson(text);
}

#if defined(LEAF_ENABLE_DEBUG)
class FileDebugObserver final : public leaf::IDebugObserver {
 public:
  explicit FileDebugObserver(std::filesystem::path directory) : directory_(std::move(directory)) {}

  void onArtifact(leaf::DebugArtifactView artifact) noexcept override {
    try {
      const char* name = "artifact.png";
      switch (artifact.stage) {
        case leaf::DebugStage::Resized: name = "resized.png"; break;
        case leaf::DebugStage::Gray: name = "gray.png"; break;
        case leaf::DebugStage::Binary: name = "binary.png"; break;
        case leaf::DebugStage::Contour: name = "contour.png"; break;
        case leaf::DebugStage::Skeleton: name = "skeleton.png"; break;
        case leaf::DebugStage::CenterVein: name = "center_vein.png"; break;
        case leaf::DebugStage::SecondaryVeins: name = "secondary_veins.png"; break;
        case leaf::DebugStage::Keypoints: name = "keypoints.png"; break;
        case leaf::DebugStage::Final: name = "final.png"; break;
      }
      if (artifact.image.data == nullptr || artifact.image.format != leaf::PixelFormat::RGB8) {
        failed_ = true;
        return;
      }
      const cv::Mat rgb(artifact.image.height, artifact.image.width, CV_8UC3,
                        const_cast<std::uint8_t*>(artifact.image.data),
                        static_cast<std::size_t>(artifact.image.stride));
      cv::Mat bgr;
      cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
      if (!cv::imwrite((directory_ / name).string(), bgr)) {
        failed_ = true;
      }
    } catch (...) {
      failed_ = true;
    }
  }

  bool failed() const { return failed_; }

 private:
  std::filesystem::path directory_;
  bool failed_ = false;
};

int emitDebug(const std::filesystem::path& directory, const leaf::cli::LoadedImage& image,
              const leaf::AnalysisResult& result) {
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error) {
    return 5;
  }
  FileDebugObserver observer(directory);
  leaf::renderDebugArtifacts(image.view, result, observer);
  return observer.failed() ? 5 : 0;
}
#endif

int analyzeOne(const leaf::Analyzer& analyzer, const std::filesystem::path& input,
               const std::filesystem::path& output, const std::filesystem::path* debugDir) {
  leaf::cli::LoadedImage image;
  const auto loaded = leaf::cli::loadImage(input.string(), image);
  if (loaded != leaf::cli::LoadStatus::Ok) {
    return 3;
  }
  const auto result = analyzer.analyze(image.view);
  if (!result.hasValue()) {
    return writeError(output, *result.error());
  }
  const auto json = leaf::analysisResultToJson(*result.value());
  if (!json.hasValue()) {
    return writeError(output, *json.error());
  }
  if (!writeAtomic(output, *json.value())) {
    return 5;
  }
#if defined(LEAF_ENABLE_DEBUG)
  if (debugDir != nullptr) {
    const int debugStatus = emitDebug(*debugDir, image, *result.value());
    if (debugStatus != 0) {
      return debugStatus;
    }
  }
#else
  (void)debugDir;
#endif
  return 0;
}

int worse(int current, int next) {
  return std::max(current, next);
}

}  // namespace

int main(int argc, char** argv) {
  Arguments args;
  const int parsed = parseArguments(argc, argv, args);
  if (parsed >= 0) {
    return parsed;
  }

  const auto config = loadConfig(args.config);
  if (!config.hasValue()) {
    const std::filesystem::path errorPath =
        args.output != nullptr ? std::filesystem::path(args.output)
                               : std::filesystem::path(args.outputDir) / "config-error.json";
    return writeError(errorPath, *config.error());
  }
  const auto analyzer = leaf::Analyzer::create(*config.value());
  if (!analyzer.hasValue()) {
    const std::filesystem::path errorPath =
        args.output != nullptr ? std::filesystem::path(args.output)
                               : std::filesystem::path(args.outputDir) / "config-error.json";
    return writeError(errorPath, *analyzer.error());
  }

  if (args.input != nullptr) {
    const std::filesystem::path debugPath = args.debug != nullptr ? std::filesystem::path(args.debug)
                                                                  : std::filesystem::path();
    return analyzeOne(**analyzer.value(), args.input, args.output,
                      args.debug != nullptr ? &debugPath : nullptr);
  }

  std::error_code directoryError;
  std::filesystem::directory_iterator cursor(args.inputDir, directoryError);
  if (directoryError) {
    std::cerr << "cannot read input directory\n";
    return 2;
  }
  std::vector<std::filesystem::path> inputs;
  for (const auto& entry : cursor) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto extension = entry.path().extension().string();
    std::string lower = extension;
    for (char& ch : lower) {
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (lower == ".jpg" || lower == ".jpeg" || lower == ".png") {
      inputs.push_back(entry.path());
    }
  }
  std::sort(inputs.begin(), inputs.end());

  int status = 0;
  for (const auto& input : inputs) {
    const std::filesystem::path output =
        std::filesystem::path(args.outputDir) / (input.stem().string() + ".json");
#if defined(LEAF_ENABLE_DEBUG)
    const std::filesystem::path debugPath =
        args.debugDir != nullptr ? std::filesystem::path(args.debugDir) / input.stem()
                                 : std::filesystem::path();
    const int one = analyzeOne(**analyzer.value(), input, output,
                               args.debugDir != nullptr ? &debugPath : nullptr);
#else
    const int one = analyzeOne(**analyzer.value(), input, output, nullptr);
#endif
    status = worse(status, one);
  }
  return status;
}
