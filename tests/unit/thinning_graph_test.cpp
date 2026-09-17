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

const leaf::detail::SkeletonNode* nodeAt(const leaf::detail::SkeletonGraph& graph,
                                         leaf::Point point) {
  for (const auto& node : graph.nodes) {
    if (node.point == point) {
      return &node;
    }
  }
  return nullptr;
}

const leaf::detail::SkeletonNode* nodeById(const leaf::detail::SkeletonGraph& graph,
                                           std::uint32_t id) {
  for (const auto& node : graph.nodes) {
    if (node.id == id) {
      return &node;
    }
  }
  return nullptr;
}

void expectSimpleUndirected(const leaf::detail::SkeletonGraph& graph) {
  for (const auto& node : graph.nodes) {
    std::vector<std::uint32_t> seen;
    for (std::uint32_t edge : node.edges) {
      EXPECT_NE(edge, node.id) << "self-loop at id " << node.id;
      EXPECT_TRUE(std::find(seen.begin(), seen.end(), edge) == seen.end())
          << "duplicate edge " << node.id << " -> " << edge;
      seen.push_back(edge);
      const auto* neighbor = nodeById(graph, edge);
      ASSERT_NE(neighbor, nullptr) << "dangling edge " << node.id << " -> " << edge;
      EXPECT_TRUE(std::find(neighbor->edges.begin(), neighbor->edges.end(), node.id) !=
                  neighbor->edges.end())
          << "missing reverse edge " << edge << " -> " << node.id;
    }
  }
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
  ASSERT_NE(merged.value(), nullptr);
  const auto& result = *merged.value();

  ASSERT_EQ(result.nodes.size(), 5u);
  const auto* survivor = nodeAt(result, {1, 2});
  ASSERT_NE(survivor, nullptr);
  EXPECT_EQ(nodeAt(result, {3, 2}), nullptr);

  const std::vector<leaf::Point> endpoints{{1, 0}, {0, 2}, {4, 2}, {3, 4}};
  ASSERT_EQ(survivor->edges.size(), 4u);
  for (const auto& point : endpoints) {
    const auto* endpoint = nodeAt(result, point);
    ASSERT_NE(endpoint, nullptr) << "missing endpoint " << point.x << "," << point.y;
    EXPECT_TRUE(std::find(survivor->edges.begin(), survivor->edges.end(), endpoint->id) !=
                survivor->edges.end());
    EXPECT_EQ(endpoint->edges, (std::vector<std::uint32_t>{survivor->id}));
  }
  expectSimpleUndirected(result);
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

TEST(skeleton, PruneLeavesCycleUnchanged) {
  const cv::Mat loop = binaryMat(
      7, 7,
      {{1, 1}, {2, 1}, {3, 1}, {4, 1}, {5, 1}, {1, 2}, {5, 2}, {1, 3}, {5, 3}, {1, 4}, {5, 4},
       {1, 5}, {2, 5}, {3, 5}, {4, 5}, {5, 5}});
  const auto graph = leaf::detail::buildSkeletonGraph(loop);
  ASSERT_TRUE(graph.hasValue());
  ASSERT_EQ(graph.value()->nodes.size(), 16u);
  const std::string before = serializeGraph(*graph.value());
  const auto pruned =
      leaf::detail::pruneAndMerge(*graph.value(), centerWithLength(100.0), detectionConfig(0.0, 1.0));
  ASSERT_TRUE(pruned.hasValue());
  EXPECT_EQ(serializeGraph(*pruned.value()), before);
  expectSimpleUndirected(*pruned.value());
}

TEST(skeleton, PruneLeavesIsolatedDegreeZeroUnchanged) {
  const cv::Mat isolated = binaryMat(5, 5, {{2, 2}});
  const auto graph = leaf::detail::buildSkeletonGraph(isolated);
  ASSERT_TRUE(graph.hasValue());
  ASSERT_EQ(graph.value()->nodes.size(), 1u);
  EXPECT_TRUE(graph.value()->nodes[0].edges.empty());
  const auto pruned =
      leaf::detail::pruneAndMerge(*graph.value(), centerWithLength(100.0), detectionConfig(0.0, 1.0));
  ASSERT_TRUE(pruned.hasValue());
  ASSERT_EQ(pruned.value()->nodes.size(), 1u);
  EXPECT_EQ(pruned.value()->nodes[0].point, (leaf::Point{2, 2}));
  EXPECT_TRUE(pruned.value()->nodes[0].edges.empty());
}

TEST(skeleton, PruneRemovesShortIsolatedLineStrictlyBelowThreshold) {
  const cv::Mat line = binaryMat(5, 7, {{2, 2}, {3, 2}, {4, 2}});
  const auto graph = leaf::detail::buildSkeletonGraph(line);
  ASSERT_TRUE(graph.hasValue());
  ASSERT_EQ(graph.value()->nodes.size(), 3u);
  // Arc length of the three-pixel line is 2.0. Prune uses strict `< threshold`.
  const auto kept = leaf::detail::pruneAndMerge(*graph.value(), centerWithLength(10.0),
                                                detectionConfig(0.0, 0.20));
  ASSERT_TRUE(kept.hasValue());
  EXPECT_EQ(nodeCoordinates(*kept.value()),
            std::vector<leaf::Point>({{2, 2}, {3, 2}, {4, 2}}));

  const auto removed = leaf::detail::pruneAndMerge(*graph.value(), centerWithLength(10.0),
                                                   detectionConfig(0.0, 0.21));
  ASSERT_TRUE(removed.hasValue());
  EXPECT_TRUE(removed.value()->nodes.empty());
}

TEST(skeleton, TAndCrossExposeEightConnectedShortcutContract) {
  // Secondary-vein stages must treat 8-connectivity as the graph contract:
  // pixels adjacent to a geometric T/cross are also degree >= 3 because of
  // diagonal shortcuts, and edges follow N,NE,E,SE,S,SW,W,NW order.
  const cv::Mat tee =
      binaryMat(7, 7, {{1, 1}, {2, 1}, {3, 1}, {4, 1}, {5, 1}, {3, 2}, {3, 3}, {3, 4}, {3, 5}});
  const auto teeGraph = leaf::detail::buildSkeletonGraph(tee);
  ASSERT_TRUE(teeGraph.hasValue());
  ASSERT_EQ(teeGraph.value()->nodes.size(), 9u);
  EXPECT_EQ(teeGraph.value()->nodes[1].edges, (std::vector<std::uint32_t>{2, 5, 0}));
  EXPECT_EQ(teeGraph.value()->nodes[2].edges, (std::vector<std::uint32_t>{3, 5, 1}));
  EXPECT_EQ(teeGraph.value()->nodes[3].edges, (std::vector<std::uint32_t>{4, 5, 2}));
  EXPECT_EQ(teeGraph.value()->nodes[5].edges, (std::vector<std::uint32_t>{2, 3, 6, 1}));
  EXPECT_EQ(teeGraph.value()->nodes[1].edges.size(), 3u);
  EXPECT_EQ(teeGraph.value()->nodes[2].edges.size(), 3u);
  EXPECT_EQ(teeGraph.value()->nodes[5].edges.size(), 4u);
  expectSimpleUndirected(*teeGraph.value());

  const cv::Mat cross =
      binaryMat(7, 7, {{3, 1}, {3, 2}, {1, 3}, {2, 3}, {3, 3}, {4, 3}, {5, 3}, {3, 4}, {3, 5}});
  const auto crossGraph = leaf::detail::buildSkeletonGraph(cross);
  ASSERT_TRUE(crossGraph.hasValue());
  ASSERT_EQ(crossGraph.value()->nodes.size(), 9u);
  EXPECT_EQ(crossGraph.value()->nodes[1].edges, (std::vector<std::uint32_t>{0, 5, 4, 3}));
  EXPECT_EQ(crossGraph.value()->nodes[3].edges, (std::vector<std::uint32_t>{1, 4, 7, 2}));
  EXPECT_EQ(crossGraph.value()->nodes[4].edges, (std::vector<std::uint32_t>{1, 5, 7, 3}));
  EXPECT_EQ(crossGraph.value()->nodes[5].edges, (std::vector<std::uint32_t>{6, 7, 4, 1}));
  EXPECT_EQ(crossGraph.value()->nodes[7].edges, (std::vector<std::uint32_t>{4, 5, 8, 3}));
  EXPECT_EQ(crossGraph.value()->nodes[4].edges.size(), 4u);
  expectSimpleUndirected(*crossGraph.value());
}
