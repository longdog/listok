#include <gtest/gtest.h>

#include <leaf/config.h>
#include <leaf/error.h>
#include <leaf/types.h>

#include <keypoint_extractor.h>
#include <polyline.h>
#include <secondary_vein_detector.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

leaf::CoordinateSystem axisCs(double scale) {
  return leaf::CoordinateSystem{{0, 0}, {1, 0}, {0, 1}, scale};
}

leaf::DetectionConfig veinConfig() {
  leaf::DetectionConfig config = leaf::defaultAnalyzerConfig().detection;
  config.minSecondaryVeinLengthNorm = 0.08;
  config.minSecondaryAngleDeg = 15;
  config.maxSecondaryAngleDeg = 165;
  config.minAttachmentSeparationNorm = 0.05;
  config.endpointContourSnapPx = 8;
  config.branchMergeRadiusPx = 1.5;
  config.minimumStageConfidence = 0;
  config.ambiguityScoreDelta = 0;
  return config;
}

leaf::LeafContour rectangleContour(double left, double right, double bottom, double top) {
  leaf::LeafContour contour;
  contour.path = {{left, bottom}, {right, bottom}, {right, top}, {left, top}, {left, bottom}};
  contour.confidence = 1;
  return contour;
}

leaf::CenterVein verticalCenter(double length) {
  leaf::CenterVein center;
  center.path = {{0, 0}, {0, length}};
  center.base = {0, 0};
  center.apex = {0, length};
  center.confidence = 1;
  return center;
}

class GraphBuilder {
 public:
  std::uint32_t add(double x, double y) {
    leaf::detail::SkeletonNode node;
    node.id = static_cast<std::uint32_t>(graph.nodes.size());
    node.point = {x, y};
    graph.nodes.push_back(node);
    return node.id;
  }

  void link(std::uint32_t a, std::uint32_t b) {
    graph.nodes[a].edges.push_back(b);
    graph.nodes[b].edges.push_back(a);
  }

  std::uint32_t chain(leaf::Point from, leaf::Point to, double step) {
    const std::uint32_t start = add(from.x, from.y);
    const double distance = std::hypot(to.x - from.x, to.y - from.y);
    const int pieces = std::max(1, static_cast<int>(std::ceil(distance / step)));
    std::uint32_t previous = start;
    for (int i = 1; i <= pieces; ++i) {
      const double t = static_cast<double>(i) / pieces;
      const std::uint32_t next = add(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t);
      link(previous, next);
      previous = next;
    }
    return start;
  }

  leaf::detail::SkeletonGraph graph;
};

leaf::detail::SkeletonGraph canonicalBranchGraph() {
  GraphBuilder builder;
  std::uint32_t previous = builder.add(0, 0);
  for (int y = 10; y <= 100; y += 10) {
    const std::uint32_t node = builder.add(0, y);
    builder.link(previous, node);
    previous = node;
  }
  builder.chain({0, 20}, {-30, 20}, 5);
  builder.chain({0, 60}, {-30, 60}, 5);
  builder.chain({0, 30}, {30, 30}, 5);
  builder.chain({0, 75}, {30, 75}, 5);
  return builder.graph;
}

double centerArcAt(const leaf::Path& path, leaf::Point point) {
  double traveled = 0;
  double best = 0;
  double bestDistance = 1e300;
  for (std::size_t i = 1; i < path.size(); ++i) {
    const double dx = path[i].x - path[i - 1].x;
    const double dy = path[i].y - path[i - 1].y;
    const double length = std::hypot(dx, dy);
    double projection = 0;
    if (length > 0) {
      projection = ((point.x - path[i - 1].x) * dx + (point.y - path[i - 1].y) * dy) / length;
      projection = std::clamp(projection, 0.0, length);
    }
    const leaf::Point nearest{path[i - 1].x + dx * (length > 0 ? projection / length : 0),
                              path[i - 1].y + dy * (length > 0 ? projection / length : 0)};
    const double distance = std::hypot(nearest.x - point.x, nearest.y - point.y);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = traveled + projection;
    }
    traveled += length;
  }
  return best;
}

void expectVeinCode(const leaf::Outcome<leaf::SecondaryVeins>& outcome, leaf::ErrorCode code) {
  ASSERT_FALSE(outcome.hasValue());
  ASSERT_NE(outcome.error(), nullptr);
  EXPECT_EQ(outcome.error()->code, code);
  EXPECT_EQ(outcome.error()->stage, leaf::Stage::SecondaryVeins);
}

}  // namespace

TEST(secondary_keypoints, OrdersByArcAndAssignsPublicKeypoints) {
  const auto cs = axisCs(100);
  const auto contour = rectangleContour(-30, 30, -5, 105);
  const auto center = verticalCenter(100);
  const auto veins =
      leaf::detail::detectSecondaryVeins(canonicalBranchGraph(), contour, center, cs, veinConfig());
  ASSERT_TRUE(veins.hasValue()) << veins.error()->message;
  EXPECT_LT(centerArcAt(center.path, veins.value()->leftFirst.attachment),
            centerArcAt(center.path, veins.value()->leftSecond.attachment));
  EXPECT_LT(cs.imageToNormalized(veins.value()->leftFirst.endpoint).x, 0);

  const auto keypoints = leaf::detail::extractKeypoints(contour, center, *veins.value(), cs);
  ASSERT_TRUE(keypoints.hasValue()) << keypoints.error()->message;
  EXPECT_DOUBLE_EQ(keypoints.value()->a1.image.x, veins.value()->leftFirst.attachment.x);
  EXPECT_DOUBLE_EQ(keypoints.value()->a1.image.y, veins.value()->leftFirst.attachment.y);
  EXPECT_DOUBLE_EQ(keypoints.value()->b2.image.x, veins.value()->rightSecond.attachment.x);
  EXPECT_DOUBLE_EQ(keypoints.value()->b2.image.y, veins.value()->rightSecond.attachment.y);
  const double half = 0.5 * leaf::detail::arcLength(center.path);
  const auto midpoint = leaf::detail::pointAtArc(center.path, half);
  ASSERT_TRUE(midpoint.hasValue());
  EXPECT_DOUBLE_EQ(keypoints.value()->f.image.x, midpoint.value()->x);
  EXPECT_DOUBLE_EQ(keypoints.value()->f.image.y, midpoint.value()->y);

  const leaf::NormalizedPoint* points[] = {
      &keypoints.value()->a1, &keypoints.value()->a2, &keypoints.value()->b1, &keypoints.value()->b2,
      &keypoints.value()->c1, &keypoints.value()->c2, &keypoints.value()->d1, &keypoints.value()->d2,
      &keypoints.value()->e,  &keypoints.value()->f,  &keypoints.value()->g1, &keypoints.value()->g2};
  for (const leaf::NormalizedPoint* point : points) {
    const leaf::Point normalized = cs.imageToNormalized(point->image);
    EXPECT_NEAR(point->normalized.x, normalized.x, 1e-9);
    EXPECT_NEAR(point->normalized.y, normalized.y, 1e-9);
  }
  EXPECT_LT(keypoints.value()->a1.normalized.x, 0);
  EXPECT_GT(keypoints.value()->a2.normalized.x, 0);
}

TEST(secondary_keypoints, CoversBoundariesAmbiguityAndInvalidKeypoints) {
  const auto cs = axisCs(100);
  const auto contour = rectangleContour(-40, 40, -5, 105);
  const auto center = verticalCenter(100);
  auto config = veinConfig();
  config.endpointContourSnapPx = 8;

  GraphBuilder exactSnap;
  exactSnap.chain({0, 0}, {0, 100}, 10);
  exactSnap.chain({0, 20}, {-32, 20}, 4);
  exactSnap.chain({0, 70}, {-32, 70}, 4);
  exactSnap.chain({0, 30}, {32, 30}, 4);
  exactSnap.chain({0, 80}, {32, 80}, 4);
  ASSERT_TRUE(leaf::detail::detectSecondaryVeins(exactSnap.graph, contour, center, cs, config).hasValue());

  GraphBuilder farSnap = exactSnap;
  farSnap.graph.nodes.clear();
  farSnap.chain({0, 0}, {0, 100}, 10);
  farSnap.chain({0, 20}, {-31.8, 20}, 4);
  farSnap.chain({0, 70}, {-32, 70}, 4);
  farSnap.chain({0, 30}, {32, 30}, 4);
  farSnap.chain({0, 80}, {32, 80}, 4);
  expectVeinCode(leaf::detail::detectSecondaryVeins(farSnap.graph, contour, center, cs, config),
                 leaf::ErrorCode::SecondaryVeinNotFound);

  GraphBuilder oneSide;
  oneSide.chain({0, 0}, {0, 100}, 10);
  oneSide.chain({0, 20}, {-30, 20}, 5);
  oneSide.chain({0, 30}, {30, 30}, 5);
  oneSide.chain({0, 80}, {30, 80}, 5);
  expectVeinCode(leaf::detail::detectSecondaryVeins(oneSide.graph, contour, center, cs, config),
                 leaf::ErrorCode::SecondaryVeinNotFound);

  auto ambiguous = veinConfig();
  ambiguous.ambiguityScoreDelta = 0.5;
  expectVeinCode(
      leaf::detail::detectSecondaryVeins(canonicalBranchGraph(), rectangleContour(-30, 30, -5, 105),
                                         center, cs, ambiguous),
      leaf::ErrorCode::AmbiguousVeins);

  const auto valid =
      leaf::detail::detectSecondaryVeins(canonicalBranchGraph(), rectangleContour(-30, 30, -5, 105),
                                         center, cs, veinConfig());
  ASSERT_TRUE(valid.hasValue());
  auto swapped = *valid.value();
  std::swap(swapped.leftFirst, swapped.leftSecond);
  const auto invalid = leaf::detail::extractKeypoints(rectangleContour(-30, 30, -5, 105), center,
                                                      swapped, cs);
  ASSERT_FALSE(invalid.hasValue());
  EXPECT_EQ(invalid.error()->code, leaf::ErrorCode::InvalidKeypoints);
  EXPECT_EQ(invalid.error()->stage, leaf::Stage::Keypoints);
}
