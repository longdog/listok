#pragma once

#include <leaf/config.h>
#include <leaf/outcome.h>

#include <array>

namespace leaf::detail {

Outcome<int> calculateScore(double k, const std::array<ScoreRange, 5>& ranges) noexcept;

}  // namespace leaf::detail
