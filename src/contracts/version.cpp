#include <leaf/config.h>
#include <leaf/types.h>

namespace leaf {
namespace {

constexpr const char kLibraryVersion[] = "1.0.0";
constexpr const char kAlgorithmVersion[] = "1.0.0";
constexpr const char kMethodologyVersion[] = "1.0.0";

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

AnalyzerConfig defaultAnalyzerConfig() noexcept { return AnalyzerConfig{}; }

}  // namespace leaf
