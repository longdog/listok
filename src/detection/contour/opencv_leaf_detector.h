#pragma once

#include <leaf/config.h>
#include <leaf/outcome.h>
#include <leaf/types.h>

#include "../../preprocess/preprocessor.h"

namespace leaf::detail {

Outcome<LeafContour> detectLeafContour(const PreprocessResult& image,
                                       const DetectionConfig& config) noexcept;

}  // namespace leaf::detail
