#pragma once
#include <leaf/config.h>
#include <leaf/outcome.h>
#include <leaf/types.h>
#include "../preprocess/preprocessor.h"
namespace leaf::detail {
Outcome<QualityAssessment> assessQuality(const Photometry& photometry, double leaf, double center,
    double secondary, double keypoints, double measurements, const QualityConfig& quality) noexcept;
}
