#pragma once

#include <leaf/outcome.h>
#include <leaf/types.h>

namespace leaf::detail {

Outcome<LeafAsymmetry> calculateAsymmetry(const LeafMeasurements& measurements,
                                          double minDenominator) noexcept;

}  // namespace leaf::detail
