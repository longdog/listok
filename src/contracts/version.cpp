#include <leaf/config.h>
#include <leaf/types.h>

namespace leaf {
namespace {

constexpr const char kLibraryVersion[] = "1.0.0";
constexpr const char kAlgorithmVersion[] = "1.0.0";
constexpr const char kMethodologyVersion[] = "1.0.0";

std::array<ScoreRange, 5> makeDefaultScoreRanges() noexcept {
  return std::array<ScoreRange, 5>{
      ScoreRange{0.000, 0.040, 1},
      ScoreRange{0.040, 0.045, 2},
      ScoreRange{0.045, 0.050, 3},
      ScoreRange{0.050, 0.055, 4},
      ScoreRange{0.055, std::nullopt, 5},
  };
}

}  // namespace

Versions defaultVersions() noexcept {
  Versions versions;
  versions.libraryVersion = kLibraryVersion;
  versions.algorithmVersion = kAlgorithmVersion;
  versions.methodologyVersion = kMethodologyVersion;
  versions.modelVersion = std::nullopt;
  return versions;
}

const char* libraryVersion() noexcept { return kLibraryVersion; }

const char* algorithmVersion() noexcept { return kAlgorithmVersion; }

const char* methodologyVersion() noexcept { return kMethodologyVersion; }

AnalyzerConfig defaultAnalyzerConfig() noexcept {
  AnalyzerConfig config;
  config.scoreRanges = makeDefaultScoreRanges();
  return config;
}

}  // namespace leaf
