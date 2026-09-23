#pragma once

#include <leaf/config.h>
#include <leaf/image.h>
#include <leaf/result.h>

#include <memory>
#include <span>

namespace leaf {

class Analyzer final {
 public:
  static Outcome<std::unique_ptr<Analyzer>> create(AnalyzerConfig config = {}) noexcept;
  Outcome<AnalysisResult> analyze(ImageView image) const noexcept;
  BatchResult analyzeBatch(std::span<const ImageView> images) const noexcept;

  ~Analyzer();
  Analyzer(Analyzer&&) noexcept;
  Analyzer& operator=(Analyzer&&) noexcept;
  Analyzer(const Analyzer&) = delete;
  Analyzer& operator=(const Analyzer&) = delete;

 private:
  class Impl;
  explicit Analyzer(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;
};

}  // namespace leaf
