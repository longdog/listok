#pragma once

#include <leaf/config.h>
#include <leaf/outcome.h>
#include <leaf/types.h>

#include <opencv2/core.hpp>

#include <cstdint>
#include <vector>

namespace leaf::detail {

struct SkeletonNode {
  std::uint32_t id{};
  Point point{};
  std::vector<std::uint32_t> edges;
};

struct SkeletonGraph {
  std::vector<SkeletonNode> nodes;
};

// One node per nonzero skeleton pixel, ids in row-major order. Neighbor edges
// are emitted in the fixed order N, NE, E, SE, S, SW, W, NW.
Outcome<SkeletonGraph> buildSkeletonGraph(const cv::Mat& skeleton) noexcept;

// Merge branch nodes within DetectionConfig::branchMergeRadiusPx (distance, then
// Y/X/id) and prune terminal paths shorter than
// minSkeletonBranchLengthNorm * arcLength(center.path). Remaining nodes are
// reindexed row-major. Config is not modified.
Outcome<SkeletonGraph> pruneAndMerge(SkeletonGraph graph, const CenterVein& center,
                                     const DetectionConfig& config) noexcept;

}  // namespace leaf::detail
