#include "golden_compare.h"

#include <gtest/gtest.h>

#include <cmath>

void expectPointWithin5Px(leaf::Point actual, leaf::Point expected) {
  const double distance = std::hypot(actual.x - expected.x, actual.y - expected.y);
  if (distance > 5.0) {
    ADD_FAILURE() << "point distance " << distance << " exceeds 5 px";
  }
}

void expectLengthWithin5Percent(double actual, double expected) {
  const double limit = 0.05 * std::abs(expected);
  if (std::abs(actual - expected) > limit) {
    ADD_FAILURE() << "length error exceeds 5 percent of " << expected;
  }
}

void expectAngleWithin3Degrees(double actual, double expected) {
  if (std::abs(actual - expected) > 3.0) {
    ADD_FAILURE() << "angle error " << std::abs(actual - expected) << " exceeds 3 degrees";
  }
}

void expectAsymmetryWithin005(double actual, double expected) {
  if (std::abs(actual - expected) > 0.005) {
    ADD_FAILURE() << "asymmetry error exceeds 0.005";
  }
}
