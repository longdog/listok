#pragma once

#include <leaf/config.h>
#include <leaf/image.h>
#include <leaf/types.h>

namespace leaf {

class ILeafDetector {
 public:
  virtual ~ILeafDetector() = default;
  virtual Outcome<LeafContour> detect(ImageView image,
                                      const DetectionConfig& config) const noexcept = 0;
};

class IVeinDetector {
 public:
  virtual ~IVeinDetector() = default;
  virtual Outcome<CenterVein> detectCenter(ImageView image,
                                           const LeafContour& leaf,
                                           const DetectionConfig& config) const noexcept = 0;
  virtual Outcome<SecondaryVeins> detectSecondary(ImageView image,
                                                  const LeafContour& leaf,
                                                  const CenterVein& centerVein,
                                                  const CoordinateSystem& coordinateSystem,
                                                  const DetectionConfig& config) const noexcept = 0;
};

}  // namespace leaf
