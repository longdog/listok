#include <leaf/analyzer.h>

#include <leaf/config.h>
#include <leaf/image.h>
#include <leaf/types.h>

#include <memory>
#include <utility>

namespace leaf {

class Analyzer::Impl {};

Analyzer::Analyzer(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

Analyzer::~Analyzer() = default;

Analyzer::Analyzer(Analyzer&&) noexcept = default;

Analyzer& Analyzer::operator=(Analyzer&&) noexcept = default;

Outcome<std::unique_ptr<Analyzer>> Analyzer::create(AnalyzerConfig config) noexcept {
  try {
    const auto validated = validateConfig(std::move(config));
    if (!validated.hasValue()) {
      return Outcome<std::unique_ptr<Analyzer>>::failure(*validated.error());
    }
    return Outcome<std::unique_ptr<Analyzer>>::success(
        std::unique_ptr<Analyzer>(new Analyzer(std::make_unique<Impl>())));
  } catch (const std::bad_alloc&) {
    return Outcome<std::unique_ptr<Analyzer>>::failure(
        {ErrorCode::InternalError, Stage::Internal, "out of memory"});
  } catch (...) {
    return Outcome<std::unique_ptr<Analyzer>>::failure(
        {ErrorCode::InternalError, Stage::Internal, "analyzer create failed"});
  }
}

Outcome<AnalysisResult> Analyzer::analyze(ImageView image) const noexcept {
  try {
    const auto validatedImage = validateImage(image);
    if (!validatedImage.hasValue()) {
      return Outcome<AnalysisResult>::failure(*validatedImage.error());
    }
    return Outcome<AnalysisResult>::failure(
        {ErrorCode::InternalError, Stage::Internal, "analyzer pipeline is not implemented"});
  } catch (const std::bad_alloc&) {
    return Outcome<AnalysisResult>::failure(
        {ErrorCode::InternalError, Stage::Internal, "out of memory"});
  } catch (...) {
    return Outcome<AnalysisResult>::failure(
        {ErrorCode::InternalError, Stage::Internal, "analyzer analyze failed"});
  }
}

BatchResult Analyzer::analyzeBatch(std::span<const ImageView> images) const noexcept {
  BatchResult batch;
  try {
    batch.versions = defaultVersions();
    batch.total = images.size();
    batch.valid = false;
    batch.score = 0;
    batch.items.reserve(images.size());
    for (const ImageView& image : images) {
      batch.items.push_back(BatchItem{analyze(image)});
      if (batch.items.back().outcome.hasValue()) {
        ++batch.successful;
      }
    }
  } catch (const std::bad_alloc&) {
    batch.items.clear();
    batch.total = images.size();
    batch.successful = 0;
    batch.acceptable = 0;
    batch.valid = false;
    batch.score = 0;
    batch.meanAsymmetry.reset();
  } catch (...) {
    batch.items.clear();
    batch.total = images.size();
    batch.successful = 0;
    batch.acceptable = 0;
    batch.valid = false;
    batch.score = 0;
    batch.meanAsymmetry.reset();
  }
  return batch;
}

}  // namespace leaf
