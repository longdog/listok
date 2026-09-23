#pragma once

#include <leaf/image.h>

namespace leaf {

enum class DebugStage {
  Resized,
  Gray,
  Binary,
  Contour,
  Skeleton,
  CenterVein,
  SecondaryVeins,
  Keypoints,
  Final,
};

struct DebugArtifactView {
  DebugStage stage;
  ImageView image;
};

class IDebugObserver {
 public:
  virtual ~IDebugObserver() = default;
  virtual void onArtifact(DebugArtifactView artifact) noexcept = 0;
};

}  // namespace leaf
