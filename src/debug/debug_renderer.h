#pragma once

#include <leaf/debug.h>
#include <leaf/image.h>
#include <leaf/result.h>

namespace leaf {

// Draws contour, center vein, axes, keypoints a1–g2 and M1–M5 onto copies of
// the source image and publishes them through the observer. Pixel buffers stay
// alive for the duration of each onArtifact call and are not written back.
void renderDebugArtifacts(ImageView image, const AnalysisResult& result,
                          IDebugObserver& observer) noexcept;

}  // namespace leaf
