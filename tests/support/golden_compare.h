#pragma once

#include <leaf/types.h>

// Inclusive normative tolerances for synthetic and labeled fixtures only.
// testdata/ has no ground truth and must not be passed to these comparators.
void expectPointWithin5Px(leaf::Point actual, leaf::Point expected);
void expectLengthWithin5Percent(double actual, double expected);
void expectAngleWithin3Degrees(double actual, double expected);
void expectAsymmetryWithin005(double actual, double expected);
