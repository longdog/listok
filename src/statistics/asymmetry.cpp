#include "asymmetry.h"

#include <cmath>

namespace leaf::detail {
namespace {

bool finiteNonNegative(double value) noexcept {
  return std::isfinite(value) && value >= 0.0;
}

bool validAngle(double degrees) noexcept {
  return std::isfinite(degrees) && degrees >= 0.0 && degrees <= 180.0;
}

Outcome<LeafAsymmetry> invalid(const char* message) noexcept {
  return Outcome<LeafAsymmetry>::failure(
      {ErrorCode::InvalidMeasurements, Stage::Statistics, message});
}

}  // namespace

Outcome<LeafAsymmetry> calculateAsymmetry(const LeafMeasurements& measurements,
                                          double minDenominator) noexcept {
  if (!std::isfinite(minDenominator) || minDenominator <= 0.0) {
    return invalid("minDenominator");
  }

  const double left[] = {measurements.left.m1, measurements.left.m2, measurements.left.m3,
                         measurements.left.m4, measurements.left.m5};
  const double right[] = {measurements.right.m1, measurements.right.m2, measurements.right.m3,
                          measurements.right.m4, measurements.right.m5};

  for (int index = 0; index < 4; ++index) {
    if (!finiteNonNegative(left[index]) || !finiteNonNegative(right[index])) {
      return invalid("nonfinite or negative length");
    }
  }
  if (!validAngle(left[4]) || !validAngle(right[4])) {
    return invalid("invalid angle");
  }

  for (int index = 0; index < 5; ++index) {
    if (std::abs(left[index] + right[index]) < minDenominator) {
      return Outcome<LeafAsymmetry>::failure(
          {ErrorCode::ZeroAsymmetryDenominator, Stage::Statistics, "zero denominator"});
    }
  }

  LeafAsymmetry asymmetry;
  double* features[] = {&asymmetry.features.m1, &asymmetry.features.m2, &asymmetry.features.m3,
                        &asymmetry.features.m4, &asymmetry.features.m5};
  double sum = 0.0;
  for (int index = 0; index < 5; ++index) {
    const double value = (left[index] - right[index]) / (left[index] + right[index]);
    *features[index] = value;
    sum += value;
  }
  asymmetry.value = sum / 5.0;
  return Outcome<LeafAsymmetry>::success(asymmetry);
}

}  // namespace leaf::detail
