#include "batch.h"

#include "score.h"

#include <cmath>

namespace leaf::detail {

BatchResult aggregateBatch(std::vector<BatchItem> items,
                           const Versions& versions,
                           const std::array<ScoreRange, 5>& ranges) noexcept {
  BatchResult batch;
  batch.versions = versions;
  batch.total = items.size();
  batch.items = std::move(items);

  double sum = 0.0;
  for (const BatchItem& item : batch.items) {
    if (!item.outcome.hasValue()) {
      continue;
    }
    ++batch.successful;
    const AnalysisResult& result = *item.outcome.value();
    if (!result.quality.acceptable || !std::isfinite(result.asymmetry.value)) {
      continue;
    }
    ++batch.acceptable;
    sum += result.asymmetry.value;
  }

  if (batch.acceptable == 0) {
    batch.meanAsymmetry = std::nullopt;
    batch.valid = false;
    batch.score = 0;
    return batch;
  }

  batch.meanAsymmetry = sum / static_cast<double>(batch.acceptable);
  const auto score = calculateScore(*batch.meanAsymmetry, ranges);
  if (!score.hasValue()) {
    batch.valid = false;
    batch.score = 0;
    return batch;
  }
  batch.valid = true;
  batch.score = *score.value();
  return batch;
}

}  // namespace leaf::detail
