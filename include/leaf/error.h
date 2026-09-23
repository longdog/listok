#pragma once

#include <string>

namespace leaf {

enum class Stage {
  Input,
  Config,
  Preprocess,
  LeafContour,
  CenterVein,
  CoordinateSystem,
  Skeleton,
  SecondaryVeins,
  Keypoints,
  Measurements,
  Statistics,
  Score,
  Quality,
  Serialization,
  Internal,
};

enum class ErrorCode {
  None,
  InvalidImage,
  UnsupportedPixelFormat,
  InvalidConfigJson,
  UnknownConfigField,
  InvalidConfigValue,
  InsufficientContrast,
  InsufficientLighting,
  PreprocessFailed,
  LeafNotFound,
  LeafTooSmall,
  LeafOutsideFrame,
  AmbiguousLeafContour,
  CentralVeinNotFound,
  AmbiguousCentralVein,
  InvalidCoordinateSystem,
  SkeletonizationFailed,
  SecondaryVeinNotFound,
  AmbiguousVeins,
  InvalidKeypoints,
  InvalidMeasurements,
  ZeroAsymmetryDenominator,
  InvalidScoreRanges,
  SerializationFailed,
  InternalError,
};

struct Error {
  ErrorCode code{ErrorCode::None};
  Stage stage{Stage::Internal};
  std::string message;
};

}  // namespace leaf
