#include "quality.h"

#include <algorithm>
#include <cmath>
#include <new>
#include "../preprocess/preprocessor.h"

namespace leaf::detail {
namespace {

bool finiteUnitInterval(double value) noexcept {
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

bool validPhotometry(const Photometry& photometry) noexcept {
    return std::isfinite(photometry.meanLuminance) &&
           photometry.meanLuminance >= 0.0 && photometry.meanLuminance <= 255.0 &&
           std::isfinite(photometry.luminanceStdDev) &&
           photometry.luminanceStdDev >= 0.0 && photometry.luminanceStdDev <= 127.5;
}

Outcome<QualityAssessment> qualityFailure(const char* message) noexcept {
    return Outcome<QualityAssessment>::failure({ErrorCode::InternalError, Stage::Quality, message});
}

}  // namespace

Outcome<QualityAssessment> assessQuality(const Photometry& photometry, double leaf, double center,
    double secondary, double keypoints, double measurements, const QualityConfig& quality) noexcept {
    try {
        if (!validPhotometry(photometry) || !finiteUnitInterval(leaf) ||
            !finiteUnitInterval(center) || !finiteUnitInterval(secondary) ||
            !finiteUnitInterval(keypoints) || !finiteUnitInterval(measurements) ||
            !finiteUnitInterval(quality.minimumLeafConfidence) ||
            !finiteUnitInterval(quality.minimumCenterVeinConfidence) ||
            !finiteUnitInterval(quality.minimumSecondaryVeinConfidence) ||
            !finiteUnitInterval(quality.minimumKeypointConfidence) ||
            !finiteUnitInterval(quality.minimumMeasurementConfidence)) {
            return qualityFailure("confidence");
        }

        QualityAssessment assessment{};
        assessment.leafConfidence = leaf;
        assessment.centerVeinConfidence = center;
        assessment.secondaryVeinConfidence = secondary;
        assessment.keypointConfidence = keypoints;
        assessment.measurementConfidence = measurements;
        assessment.overallConfidence = std::min({leaf, center, secondary, keypoints, measurements, 1.0});
        assessment.sufficientContrast = photometry.acceptableContrast;
        assessment.sufficientLighting = photometry.acceptableLighting;

        if (leaf < quality.minimumLeafConfidence) {
            assessment.issues.push_back(QualityIssue::LowLeafConfidence);
        }
        if (center < quality.minimumCenterVeinConfidence) {
            assessment.issues.push_back(QualityIssue::LowCenterVeinConfidence);
        }
        if (secondary < quality.minimumSecondaryVeinConfidence) {
            assessment.issues.push_back(QualityIssue::LowSecondaryVeinConfidence);
        }
        if (keypoints < quality.minimumKeypointConfidence) {
            assessment.issues.push_back(QualityIssue::LowKeypointConfidence);
        }
        if (measurements < quality.minimumMeasurementConfidence) {
            assessment.issues.push_back(QualityIssue::LowMeasurementConfidence);
        }
        if (!photometry.acceptableContrast) {
            assessment.issues.push_back(QualityIssue::MarginalContrast);
        }
        if (!photometry.acceptableLighting) {
            assessment.issues.push_back(QualityIssue::MarginalLighting);
        }
        assessment.acceptable = assessment.issues.empty();
        return Outcome<QualityAssessment>::success(std::move(assessment));
    } catch (const std::bad_alloc&) {
        return qualityFailure("allocation");
    } catch (...) {
        return qualityFailure("internal");
    }
}

}  // namespace leaf::detail
