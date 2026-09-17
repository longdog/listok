#include <gtest/gtest.h>

#include <leaf/config.h>
#include <leaf/error.h>
#include <leaf/outcome.h>
#include <leaf/types.h>

#include "graph.h"
#include "thinning.h"

#include <opencv2/core.hpp>

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace leaf {

bool operator==(const Point& a, const Point& b) { return a.x == b.x && a.y == b.y; }

void PrintTo(const Point& point, std::ostream* os) {
  *os << '(' << point.x << ',' << point.y << ')';
}

}  // namespace leaf

namespace {

std::vector<leaf::Point> nodeCoordinates(const leaf::detail::SkeletonGraph& graph) {
  std::vector<leaf::Point> points;
  points.reserve(graph.nodes.size());
  for (const auto& node : graph.nodes) {
    points.push_back(node.point);
  }
  return points;
}

std::string serializeGraph(const leaf::detail::SkeletonGraph& graph) {
  std::ostringstream out;
  out << std::setprecision(17);
  for (const auto& node : graph.nodes) {
    out << node.id << ':' << node.point.x << ',' << node.point.y << '#';
    for (std::size_t i = 0; i < node.edges.size(); ++i) {
      if (i != 0) {
        out << ',';
      }
      out << node.edges[i];
    }
    out << ';';
  }
  return out.str();
}

cv::Mat binaryMat(int rows, int cols, const std::vector<std::pair<int, int>>& ones) {
  cv::Mat mat = cv::Mat::zeros(rows, cols, CV_8UC1);
  for (const auto& [x, y] : ones) {
    mat.at<uchar>(y, x) = 1;
  }
  return mat;
}

void expectSkeletonError(const leaf::Outcome<cv::Mat>& outcome) {
  ASSERT_FALSE(outcome.hasValue());
  ASSERT_NE(outcome.error(), nullptr);
  EXPECT_EQ(outcome.error()->code, leaf::ErrorCode::SkeletonizationFailed);
  EXPECT_EQ(outcome.error()->stage, leaf::Stage::Skeleton);
}

void expectGraphError(const leaf::Outcome<leaf::detail::SkeletonGraph>& outcome) {
  ASSERT_FALSE(outcome.hasValue());
  ASSERT_NE(outcome.error(), nullptr);
  EXPECT_EQ(outcome.error()->code, leaf::ErrorCode::SkeletonizationFailed);
  EXPECT_EQ(outcome.error()->stage, leaf::Stage::Skeleton);
}

leaf::CenterVein centerWithLength(double length) {
  leaf::CenterVein vein;
  vein.path = {{0.0, 0.0}, {0.0, length}};
  vein.base = {0.0, 0.0};
  vein.apex = {0.0, length};
  vein.confidence = 1.0;
  return vein;
}

leaf::DetectionConfig detectionConfig(double mergeRadius, double minBranchNorm) {
  leaf::DetectionConfig config;
  config.branchMergeRadiusPx = mergeRadius;
  config.minSkeletonBranchLengthNorm = minBranchNorm;
  return config;
}

}  // namespace

TEST(skeleton, ThinsSimultaneouslyAndBuildsStableGraph) {
  cv::Mat in = (cv::Mat_<uchar>(5, 5) << 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1,
                0, 0, 0, 1, 0, 0);
  auto a = leaf::detail::zhangSuen(in), b = leaf::detail::zhangSuen(in);
  ASSERT_TRUE(a.hasValue());
  ASSERT_NE(a.value(), nullptr);
  EXPECT_EQ(cv::countNonZero(*a.value() != *b.value()), 0);
  auto g = leaf::detail::buildSkeletonGraph(*a.value());
  ASSERT_TRUE(g.hasValue());
  ASSERT_NE(g.value(), nullptr);
  // Unmodified interior Zhang–Suen cannot delete image-border pixels, so the
  // 5x5 diamond thins to a 9-pixel plus rather than the interior 5-pixel subset.
  EXPECT_EQ(nodeCoordinates(*g.value()),
            std::vector<leaf::Point>(
                {{2, 0}, {2, 1}, {0, 2}, {1, 2}, {2, 2}, {3, 2}, {4, 2}, {2, 3}, {2, 4}}));
}

TEST(skeleton, EmptyBinaryYieldsEmptyGraph) {
  const cv::Mat zeros = cv::Mat::zeros(4, 4, CV_8UC1);
  const auto thin = leaf::detail::zhangSuen(zeros);
  ASSERT_TRUE(thin.hasValue());
  EXPECT_EQ(cv::countNonZero(*thin.value()), 0);
  const auto graph = leaf::detail::buildSkeletonGraph(*thin.value());
  ASSERT_TRUE(graph.hasValue());
  EXPECT_TRUE(graph.value()->nodes.empty());
}

TEST(skeleton, IsolatedPixelRemains) {
  const cv::Mat in = binaryMat(5, 5, {{2, 2}});
  const auto thin = leaf::detail::zhangSuen(in);
  ASSERT_TRUE(thin.hasValue());
  EXPECT_EQ(cv::countNonZero(*thin.value()), 1);
  EXPECT_EQ(thin.value()->at<uchar>(2, 2), 1);
  const auto graph = leaf::detail::buildSkeletonGraph(*thin.value());
  ASSERT_TRUE(graph.hasValue());
  ASSERT_EQ(graph.value()->nodes.size(), 1u);
  EXPECT_EQ(graph.value()->nodes[0].id, 0u);
  EXPECT_EQ(graph.value()->nodes[0].point, (leaf::Point{2, 2}));
  EXPECT_TRUE(graph.value()->nodes[0].edges.empty());
}

TEST(skeleton, BorderPixelIsNotDeleted) {
  const cv::Mat in = binaryMat(5, 5, {{0, 0}, {4, 2}});
  const auto thin = leaf::detail::zhangSuen(in);
  ASSERT_TRUE(thin.hasValue());
  EXPECT_EQ(thin.value()->at<uchar>(0, 0), 1);
  EXPECT_EQ(thin.value()->at<uchar>(2, 4), 1);
}

TEST(skeleton, HorizontalLineIsStableAndPathOrdered) {
  const cv::Mat in = binaryMat(5, 9, {{1, 2}, {2, 2}, {3, 2}, {4, 2}, {5, 2}, {6, 2}, {7, 2}});
  const auto thin = leaf::detail::zhangSuen(in);
  ASSERT_TRUE(thin.hasValue());
  EXPECT_EQ(cv::countNonZero(*thin.value() != in), 0);
  const auto graph = leaf::detail::buildSkeletonGraph(*thin.value());
  ASSERT_TRUE(graph.hasValue());
  ASSERT_EQ(graph.value()->nodes.size(), 7u);
  EXPECT_EQ(graph.value()->nodes.front().edges, (std::vector<std::uint32_t>{1}));
  EXPECT_EQ(graph.value()->nodes[3].edges, (std::vector<std::uint32_t>{4, 2}));
  EXPECT_EQ(graph.value()->nodes.back().edges, (std::vector<std::uint32_t>{5}));
}

TEST(skeleton, TJunctionKeepsStemAndBar) {
  const cv::Mat in = binaryMat(7, 7, {{1, 1}, {2, 1}, {3, 1}, {4, 1}, {5, 1}, {3, 2}, {3, 3}, {3, 4}, {3, 5}});
  const auto thin = leaf::detail::zhangSuen(in);
  ASSERT_TRUE(thin.hasValue());
  EXPECT_EQ(cv::countNonZero(*thin.value() != in), 0);
  const auto graph = leaf::detail::buildSkeletonGraph(*thin.value());
  ASSERT_TRUE(graph.hasValue());
  EXPECT_EQ(nodeCoordinates(*graph.value()),
            std::vector<leaf::Point>(
                {{1, 1}, {2, 1}, {3, 1}, {4, 1}, {5, 1}, {3, 2}, {3, 3}, {3, 4}, {3, 5}}));
}

TEST(skeleton, CrossLoopAndDiagonalKeepConnectivity) {
  const cv::Mat cross = binaryMat(7, 7, {{3, 1}, {3, 2}, {1, 3}, {2, 3}, {3, 3}, {4, 3}, {5, 3}, {3, 4}, {3, 5}});
  const auto thinCross = leaf::detail::zhangSuen(cross);
  ASSERT_TRUE(thinCross.hasValue());
  EXPECT_EQ(cv::countNonZero(*thinCross.value() != cross), 0);

  const cv::Mat loop = binaryMat(7, 7, {{1, 1}, {2, 1}, {3, 1}, {4, 1}, {5, 1}, {1, 2}, {5, 2}, {1, 3}, {5, 3}, {1, 4}, {5, 4}, {1, 5}, {2, 5}, {3, 5}, {4, 5}, {5, 5}});
  const auto thinLoop = leaf::detail::zhangSuen(loop);
  ASSERT_TRUE(thinLoop.hasValue());
  EXPECT_EQ(cv::countNonZero(*thinLoop.value() != loop), 0);
  const auto loopGraph = leaf::detail::buildSkeletonGraph(*thinLoop.value());
  ASSERT_TRUE(loopGraph.hasValue());
  EXPECT_EQ(loopGraph.value()->nodes.size(), 16u);
  for (const auto& node : loopGraph.value()->nodes) {
    EXPECT_GE(node.edges.size(), 2u);
  }

  const cv::Mat diagonal = binaryMat(6, 6, {{1, 1}, {2, 2}, {3, 3}, {4, 4}});
  const auto thinDiag = leaf::detail::zhangSuen(diagonal);
  ASSERT_TRUE(thinDiag.hasValue());
  EXPECT_EQ(cv::countNonZero(*thinDiag.value() != diagonal), 0);
  const auto diagGraph = leaf::detail::buildSkeletonGraph(*thinDiag.value());
  ASSERT_TRUE(diagGraph.hasValue());
  ASSERT_EQ(diagGraph.value()->nodes.size(), 4u);
  EXPECT_EQ(diagGraph.value()->nodes[0].edges, (std::vector<std::uint32_t>{1}));
  EXPECT_EQ(diagGraph.value()->nodes[1].edges, (std::vector<std::uint32_t>{2, 0}));
}

TEST(skeleton, NeighborOrderIsNESWClockwiseFromNorth) {
  const cv::Mat block = cv::Mat::ones(3, 3, CV_8UC1);
  const auto graph = leaf::detail::buildSkeletonGraph(block);
  ASSERT_TRUE(graph.hasValue());
  ASSERT_EQ(graph.value()->nodes.size(), 9u);
  EXPECT_EQ(graph.value()->nodes[4].id, 4u);
  EXPECT_EQ(graph.value()->nodes[4].point, (leaf::Point{1, 1}));
  EXPECT_EQ(graph.value()->nodes[4].edges,
            (std::vector<std::uint32_t>{1, 2, 5, 8, 7, 6, 3, 0}));
}

TEST(skeleton, NormalizesNonzeroInputAndLeavesCallersMatUnchanged) {
  cv::Mat in = (cv::Mat_<uchar>(3, 3) << 0, 255, 0, 255, 255, 255, 0, 255, 0);
  const cv::Mat original = in.clone();
  const auto thin = leaf::detail::zhangSuen(in);
  ASSERT_TRUE(thin.hasValue());
  EXPECT_EQ(cv::countNonZero(in != original), 0);
  EXPECT_EQ(cv::countNonZero(*thin.value() > 1), 0);
  const cv::Mat ones = (cv::Mat_<uchar>(3, 3) << 0, 1, 0, 1, 1, 1, 0, 1, 0);
  const auto thinOnes = leaf::detail::zhangSuen(ones);
  ASSERT_TRUE(thinOnes.hasValue());
  EXPECT_EQ(cv::countNonZero(*thin.value() != *thinOnes.value()), 0);
}

TEST(skeleton, RejectsWrongTypeAndDimensions) {
  expectSkeletonError(leaf::detail::zhangSuen(cv::Mat()));
  expectSkeletonError(leaf::detail::zhangSuen(cv::Mat::zeros(4, 4, CV_32FC1)));
  expectSkeletonError(leaf::detail::zhangSuen(cv::Mat::zeros(4, 4, CV_8UC3)));
  expectGraphError(leaf::detail::buildSkeletonGraph(cv::Mat()));
  expectGraphError(leaf::detail::buildSkeletonGraph(cv::Mat::zeros(2, 2, CV_8UC2)));
}

TEST(skeleton, RepeatedRunsAreByteIdentical) {
  const cv::Mat in = (cv::Mat_<uchar>(5, 5) << 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1,
                      1, 1, 0, 0, 0, 1, 0, 0);
  const auto firstThin = leaf::detail::zhangSuen(in);
  ASSERT_TRUE(firstThin.hasValue());
  const auto firstGraph = leaf::detail::buildSkeletonGraph(*firstThin.value());
  ASSERT_TRUE(firstGraph.hasValue());
  const std::string expected = serializeGraph(*firstGraph.value());
  const auto pruned = leaf::detail::pruneAndMerge(*firstGraph.value(), centerWithLength(100.0),
                                                  detectionConfig(0.0, 0.04));
  ASSERT_TRUE(pruned.hasValue());
  const std::string expectedPruned = serializeGraph(*pruned.value());

  for (int i = 0; i < 25; ++i) {
    const auto thin = leaf::detail::zhangSuen(in);
    ASSERT_TRUE(thin.hasValue());
    EXPECT_EQ(cv::countNonZero(*thin.value() != *firstThin.value()), 0);
    const auto graph = leaf::detail::buildSkeletonGraph(*thin.value());
    ASSERT_TRUE(graph.hasValue());
    EXPECT_EQ(serializeGraph(*graph.value()), expected);
    const auto again = leaf::detail::pruneAndMerge(*graph.value(), centerWithLength(100.0),
                                                   detectionConfig(0.0, 0.04));
    ASSERT_TRUE(again.hasValue());
    EXPECT_EQ(serializeGraph(*again.value()), expectedPruned);
  }
}

TEST(skeleton, PrunesShortSpurAndKeepsLongStem) {
  leaf::detail::SkeletonGraph graph;
  graph.nodes = {
      {0, {0, 0}, {1}},
      {1, {1, 0}, {0, 2}},
      {2, {2, 0}, {1, 3}},
      {3, {3, 0}, {2, 4}},
      {4, {4, 0}, {3, 5}},
      {5, {5, 0}, {4, 6, 7}},
      {6, {6, 0}, {5}},
      {7, {5, 1}, {5, 8}},
      {8, {5, 2}, {7, 9}},
      {9, {5, 3}, {8, 10}},
      {10, {5, 4}, {9}},
  };
  const auto result =
      leaf::detail::pruneAndMerge(graph, centerWithLength(100.0), detectionConfig(0.0, 0.04));
  ASSERT_TRUE(result.hasValue());
  EXPECT_EQ(nodeCoordinates(*result.value()),
            std::vector<leaf::Point>(
                {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {5, 1}, {5, 2}, {5, 3}, {5, 4}}));
}

TEST(skeleton, MergesNearbyBranchNodesByDistanceThenYXId) {
  leaf::detail::SkeletonGraph graph;
  graph.nodes = {
      {0, {0, 2}, {2}}, {1, {4, 2}, {3}}, {2, {1, 2}, {0, 3, 4}}, {3, {3, 2}, {1, 2, 5}},
      {4, {1, 0}, {2}}, {5, {3, 4}, {3}},
  };
  const auto merged =
      leaf::detail::pruneAndMerge(graph, centerWithLength(1000.0), detectionConfig(4.0, 1e-9));
  ASSERT_TRUE(merged.hasValue());
  const auto kept = nodeCoordinates(*merged.value());
  EXPECT_TRUE(std::find(kept.begin(), kept.end(), leaf::Point{1, 2}) != kept.end());
  EXPECT_TRUE(std::find(kept.begin(), kept.end(), leaf::Point{3, 2}) == kept.end());
}

TEST(skeleton, PruneRemovesBorderTipsOfThinnedPlus) {
  cv::Mat in = (cv::Mat_<uchar>(5, 5) << 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1,
                0, 0, 0, 1, 0, 0);
  const auto thin = leaf::detail::zhangSuen(in);
  ASSERT_TRUE(thin.hasValue());
  const auto graph = leaf::detail::buildSkeletonGraph(*thin.value());
  ASSERT_TRUE(graph.hasValue());
  const auto pruned =
      leaf::detail::pruneAndMerge(*graph.value(), centerWithLength(100.0), detectionConfig(0.0, 0.04));
  ASSERT_TRUE(pruned.hasValue());
  EXPECT_EQ(nodeCoordinates(*pruned.value()),
            std::vector<leaf::Point>({{2, 1}, {1, 2}, {2, 2}, {3, 2}, {2, 3}}));
}

TEST(skeleton, DoesNotAssumeEightConnectedPlusIsFourConnected) {
  const cv::Mat plus = binaryMat(5, 5, {{2, 1}, {1, 2}, {2, 2}, {3, 2}, {2, 3}});
  const auto graph = leaf::detail::buildSkeletonGraph(plus);
  ASSERT_TRUE(graph.hasValue());
  ASSERT_EQ(graph.value()->nodes.size(), 5u);
  EXPECT_EQ(graph.value()->nodes[0].edges, (std::vector<std::uint32_t>{3, 2, 1}));
}
