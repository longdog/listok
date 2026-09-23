#include "score.h"

#include <cmath>
#include <optional>

namespace leaf::detail {
namespace {

Outcome<int> invalidRanges(const char* message) noexcept {
  return Outcome<int>::failure({ErrorCode::InvalidScoreRanges, Stage::Score, message});
}

Outcome<int> invalidInput(const char* message) noexcept {
  return Outcome<int>::failure({ErrorCode::InvalidMeasurements, Stage::Score, message});
}

bool rangesAreValid(const std::array<ScoreRange, 5>& ranges) noexcept {
  for (std::size_t index = 0; index < ranges.size(); ++index) {
    const ScoreRange& range = ranges[index];
    if (range.score != static_cast<int>(index) + 1 || !std::isfinite(range.min)) {
      return false;
    }
    if (index == 0) {
      if (range.min != 0.0) {
        return false;
      }
    } else if (!ranges[index - 1].max.has_value() || range.min != *ranges[index - 1].max) {
      return false;
    }

    const bool isLast = index + 1 == ranges.size();
    if (isLast) {
      if (range.max.has_value()) {
        return false;
      }
      continue;
    }
    if (!range.max.has_value() || !std::isfinite(*range.max) || *range.max <= range.min) {
      return false;
    }
  }
  return true;
}

}  // namespace

Outcome<int> calculateScore(double k, const std::array<ScoreRange, 5>& ranges) noexcept {
  if (!rangesAreValid(ranges)) {
    return invalidRanges("score ranges");
  }
  if (!std::isfinite(k)) {
    return invalidInput("nonfinite asymmetry");
  }

  const double magnitude = std::abs(k);
  for (const ScoreRange& range : ranges) {
    if (!range.max.has_value()) {
      if (magnitude >= range.min) {
        return Outcome<int>::success(range.score);
      }
      break;
    }
    if (magnitude >= range.min && magnitude < *range.max) {
      return Outcome<int>::success(range.score);
    }
  }
  return invalidRanges("uncovered asymmetry");
}

}  // namespace leaf::detail
