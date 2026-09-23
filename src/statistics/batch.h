#pragma once

#include <leaf/config.h>
#include <leaf/result.h>
#include <leaf/types.h>

#include <array>
#include <vector>

namespace leaf::detail {

BatchResult aggregateBatch(std::vector<BatchItem> items,
                           const Versions& versions,
                           const std::array<ScoreRange, 5>& ranges) noexcept;

}  // namespace leaf::detail
