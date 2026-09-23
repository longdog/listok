#include "graph.h"

#include "../geometry/polyline.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace leaf::detail {
namespace {

constexpr int kNeighborDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
constexpr int kNeighborDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};

Outcome<SkeletonGraph> skeletonFailure(const char* message) {
  return Outcome<SkeletonGraph>::failure(
      {ErrorCode::SkeletonizationFailed, Stage::Skeleton, message});
}

bool isBinaryU8(const cv::Mat& mat) {
  return mat.dims == 2 && !mat.empty() && mat.type() == CV_8UC1;
}

double pointDistance(Point a, Point b) {
  return std::hypot(a.x - b.x, a.y - b.y);
}

bool pointLess(Point a, Point b) {
  if (a.y != b.y) {
    return a.y < b.y;
  }
  return a.x < b.x;
}

int nodeIndex(const SkeletonGraph& graph, std::uint32_t id) {
  for (int i = 0; i < static_cast<int>(graph.nodes.size()); ++i) {
    if (graph.nodes[static_cast<std::size_t>(i)].id == id) {
      return i;
    }
  }
  return -1;
}

void uniquePreserve(std::vector<std::uint32_t>& values) {
  std::vector<std::uint32_t> unique;
  unique.reserve(values.size());
  for (std::uint32_t value : values) {
    if (std::find(unique.begin(), unique.end(), value) == unique.end()) {
      unique.push_back(value);
    }
  }
  values = std::move(unique);
}

void orderEdges(SkeletonGraph& graph) {
  for (SkeletonNode& node : graph.nodes) {
    uniquePreserve(node.edges);
    std::vector<std::uint32_t> ordered;
    ordered.reserve(node.edges.size());
    std::vector<char> used(node.edges.size(), 0);

    auto mark = [&](std::uint32_t id) {
      for (std::size_t i = 0; i < node.edges.size(); ++i) {
        if (!used[i] && node.edges[i] == id) {
          used[i] = 1;
          return;
        }
      }
    };

    for (int dir = 0; dir < 8; ++dir) {
      const double nx = node.point.x + static_cast<double>(kNeighborDx[dir]);
      const double ny = node.point.y + static_cast<double>(kNeighborDy[dir]);
      std::uint32_t found = std::numeric_limits<std::uint32_t>::max();
      for (std::uint32_t edge : node.edges) {
        const int index = nodeIndex(graph, edge);
        if (index < 0) {
          continue;
        }
        const Point p = graph.nodes[static_cast<std::size_t>(index)].point;
        if (p.x == nx && p.y == ny) {
          found = edge;
          break;
        }
      }
      if (found != std::numeric_limits<std::uint32_t>::max()) {
        if (std::find(ordered.begin(), ordered.end(), found) == ordered.end()) {
          ordered.push_back(found);
          mark(found);
        }
      }
    }

    for (std::size_t i = 0; i < node.edges.size(); ++i) {
      if (!used[i]) {
        ordered.push_back(node.edges[i]);
      }
    }
    node.edges = std::move(ordered);
  }
}

void reindexRowMajor(SkeletonGraph& graph) {
  std::vector<std::size_t> order(graph.nodes.size());
  for (std::size_t i = 0; i < order.size(); ++i) {
    order[i] = i;
  }
  std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    const SkeletonNode& left = graph.nodes[a];
    const SkeletonNode& right = graph.nodes[b];
    if (pointLess(left.point, right.point)) {
      return true;
    }
    if (pointLess(right.point, left.point)) {
      return false;
    }
    return left.id < right.id;
  });

  std::vector<std::uint32_t> oldToNew(graph.nodes.size(), 0);
  std::vector<SkeletonNode> next;
  next.reserve(graph.nodes.size());
  for (std::size_t i = 0; i < order.size(); ++i) {
    oldToNew[order[i]] = static_cast<std::uint32_t>(i);
  }
  for (std::size_t i = 0; i < order.size(); ++i) {
    SkeletonNode node = graph.nodes[order[i]];
    node.id = static_cast<std::uint32_t>(i);
    std::vector<std::uint32_t> edges;
    edges.reserve(node.edges.size());
    for (std::uint32_t edge : node.edges) {
      const int index = nodeIndex(graph, edge);
      if (index < 0) {
        continue;
      }
      edges.push_back(oldToNew[static_cast<std::size_t>(index)]);
    }
    node.edges = std::move(edges);
    next.push_back(std::move(node));
  }
  graph.nodes = std::move(next);
  orderEdges(graph);
}

struct UnionFind {
  std::vector<int> parent;

  explicit UnionFind(int count) : parent(static_cast<std::size_t>(count)) {
    for (int i = 0; i < count; ++i) {
      parent[static_cast<std::size_t>(i)] = i;
    }
  }

  int find(int x) {
    if (parent[static_cast<std::size_t>(x)] != x) {
      parent[static_cast<std::size_t>(x)] = find(parent[static_cast<std::size_t>(x)]);
    }
    return parent[static_cast<std::size_t>(x)];
  }

  void unite(int a, int b) {
    a = find(a);
    b = find(b);
    if (a == b) {
      return;
    }
    if (a < b) {
      parent[static_cast<std::size_t>(b)] = a;
    } else {
      parent[static_cast<std::size_t>(a)] = b;
    }
  }
};

int survivorOf(const SkeletonGraph& graph, UnionFind& ufind, int index) {
  const int root = ufind.find(index);
  int survivor = root;
  for (int i = 0; i < static_cast<int>(graph.nodes.size()); ++i) {
    if (ufind.find(i) != root) {
      continue;
    }
    const SkeletonNode& candidate = graph.nodes[static_cast<std::size_t>(i)];
    const SkeletonNode& current = graph.nodes[static_cast<std::size_t>(survivor)];
    if (pointLess(candidate.point, current.point) ||
        (candidate.point.x == current.point.x && candidate.point.y == current.point.y &&
         candidate.id < current.id)) {
      survivor = i;
    }
  }
  return survivor;
}

void mergeBranches(SkeletonGraph& graph, double radius) {
  if (!(radius > 0.0) || graph.nodes.size() < 2) {
    return;
  }

  std::vector<int> branches;
  for (int i = 0; i < static_cast<int>(graph.nodes.size()); ++i) {
    if (graph.nodes[static_cast<std::size_t>(i)].edges.size() >= 3) {
      branches.push_back(i);
    }
  }

  // Pair order does not affect the union: every in-radius branch pair is united,
  // and the survivor is chosen by Y/X/id, not by candidate ranking.
  UnionFind ufind(static_cast<int>(graph.nodes.size()));
  for (std::size_t i = 0; i < branches.size(); ++i) {
    for (std::size_t j = i + 1; j < branches.size(); ++j) {
      const int a = branches[i];
      const int b = branches[j];
      const SkeletonNode& left = graph.nodes[static_cast<std::size_t>(a)];
      const SkeletonNode& right = graph.nodes[static_cast<std::size_t>(b)];
      if (pointDistance(left.point, right.point) <= radius) {
        ufind.unite(a, b);
      }
    }
  }

  std::vector<int> remap(graph.nodes.size());
  for (int i = 0; i < static_cast<int>(graph.nodes.size()); ++i) {
    remap[static_cast<std::size_t>(i)] = survivorOf(graph, ufind, i);
  }

  std::vector<std::vector<std::uint32_t>> combined(graph.nodes.size());
  for (int i = 0; i < static_cast<int>(graph.nodes.size()); ++i) {
    const int survivor = remap[static_cast<std::size_t>(i)];
    for (std::uint32_t edge : graph.nodes[static_cast<std::size_t>(i)].edges) {
      const int edgeIndex = nodeIndex(graph, edge);
      if (edgeIndex < 0) {
        continue;
      }
      const int mapped = remap[static_cast<std::size_t>(edgeIndex)];
      if (mapped == survivor) {
        continue;
      }
      combined[static_cast<std::size_t>(survivor)].push_back(
          graph.nodes[static_cast<std::size_t>(mapped)].id);
    }
  }

  std::vector<SkeletonNode> next;
  std::vector<char> keep(graph.nodes.size(), 0);
  for (int i = 0; i < static_cast<int>(graph.nodes.size()); ++i) {
    if (remap[static_cast<std::size_t>(i)] == i) {
      keep[static_cast<std::size_t>(i)] = 1;
    }
  }
  for (int i = 0; i < static_cast<int>(graph.nodes.size()); ++i) {
    if (!keep[static_cast<std::size_t>(i)]) {
      continue;
    }
    SkeletonNode node = graph.nodes[static_cast<std::size_t>(i)];
    node.edges = combined[static_cast<std::size_t>(i)];
    next.push_back(std::move(node));
  }
  graph.nodes = std::move(next);
  reindexRowMajor(graph);
}

const std::vector<std::uint32_t>& neighborsOf(const SkeletonGraph& graph, std::size_t index) {
  return graph.nodes[index].edges;
}

int degreeOf(const SkeletonGraph& graph, std::size_t index) {
  return static_cast<int>(graph.nodes[index].edges.size());
}

int indexById(const SkeletonGraph& graph, std::uint32_t id) {
  return nodeIndex(graph, id);
}

void pruneShortTerminals(SkeletonGraph& graph, double threshold) {
  if (graph.nodes.empty() || !(threshold > 0.0)) {
    return;
  }

  while (true) {
    const int n = static_cast<int>(graph.nodes.size());
    std::vector<char> remove(static_cast<std::size_t>(n), 0);
    bool marked = false;

    auto walkAndMark = [&](int start) {
      if (remove[static_cast<std::size_t>(start)]) {
        return;
      }
      std::vector<int> chain;
      chain.push_back(start);
      int prev = -1;
      int current = start;
      double length = 0.0;
      std::vector<char> seen(static_cast<std::size_t>(n), 0);
      seen[static_cast<std::size_t>(current)] = 1;

      while (true) {
        if (degreeOf(graph, static_cast<std::size_t>(current)) >= 3 && current != start) {
          chain.pop_back();
          break;
        }

        int next = -1;
        for (std::uint32_t edge : neighborsOf(graph, static_cast<std::size_t>(current))) {
          const int index = indexById(graph, edge);
          if (index < 0 || index == prev) {
            continue;
          }
          if (next < 0) {
            next = index;
          }
        }

        if (next < 0) {
          break;
        }
        if (degreeOf(graph, static_cast<std::size_t>(next)) >= 3) {
          length += pointDistance(graph.nodes[static_cast<std::size_t>(current)].point,
                                  graph.nodes[static_cast<std::size_t>(next)].point);
          break;
        }
        if (seen[static_cast<std::size_t>(next)]) {
          break;
        }
        length += pointDistance(graph.nodes[static_cast<std::size_t>(current)].point,
                                graph.nodes[static_cast<std::size_t>(next)].point);
        prev = current;
        current = next;
        seen[static_cast<std::size_t>(current)] = 1;
        chain.push_back(current);
        if (degreeOf(graph, static_cast<std::size_t>(current)) <= 1 && current != start) {
          break;
        }
      }

      if (!chain.empty() && length < threshold) {
        for (int index : chain) {
          remove[static_cast<std::size_t>(index)] = 1;
          marked = true;
        }
      }
    };

    for (int i = 0; i < n; ++i) {
      if (degreeOf(graph, static_cast<std::size_t>(i)) == 1) {
        walkAndMark(i);
      }
    }

    if (!marked) {
      break;
    }

    std::vector<SkeletonNode> next;
    for (int i = 0; i < n; ++i) {
      if (remove[static_cast<std::size_t>(i)]) {
        continue;
      }
      SkeletonNode node = graph.nodes[static_cast<std::size_t>(i)];
      std::vector<std::uint32_t> edges;
      for (std::uint32_t edge : node.edges) {
        const int index = indexById(graph, edge);
        if (index < 0 || remove[static_cast<std::size_t>(index)]) {
          continue;
        }
        edges.push_back(edge);
      }
      node.edges = std::move(edges);
      next.push_back(std::move(node));
    }
    graph.nodes = std::move(next);
    reindexRowMajor(graph);
  }
}

}  // namespace

Outcome<SkeletonGraph> buildSkeletonGraph(const cv::Mat& skeleton) noexcept {
  try {
    if (!isBinaryU8(skeleton)) {
      return skeletonFailure("invalid skeleton image");
    }

    std::vector<int> idAt(static_cast<std::size_t>(skeleton.rows) *
                              static_cast<std::size_t>(skeleton.cols),
                          -1);
    SkeletonGraph graph;
    for (int y = 0; y < skeleton.rows; ++y) {
      const uchar* row = skeleton.ptr<uchar>(y);
      for (int x = 0; x < skeleton.cols; ++x) {
        if (row[x] == 0) {
          continue;
        }
        const auto id = static_cast<std::uint32_t>(graph.nodes.size());
        idAt[static_cast<std::size_t>(y) * static_cast<std::size_t>(skeleton.cols) +
             static_cast<std::size_t>(x)] = static_cast<int>(id);
        graph.nodes.push_back(SkeletonNode{id, Point{static_cast<double>(x), static_cast<double>(y)}, {}});
      }
    }

    auto idAtPixel = [&](int y, int x) -> int {
      if (y < 0 || x < 0 || y >= skeleton.rows || x >= skeleton.cols) {
        return -1;
      }
      return idAt[static_cast<std::size_t>(y) * static_cast<std::size_t>(skeleton.cols) +
                  static_cast<std::size_t>(x)];
    };

    for (SkeletonNode& node : graph.nodes) {
      const int x = static_cast<int>(node.point.x);
      const int y = static_cast<int>(node.point.y);
      for (int dir = 0; dir < 8; ++dir) {
        const int neighbor = idAtPixel(y + kNeighborDy[dir], x + kNeighborDx[dir]);
        if (neighbor >= 0) {
          node.edges.push_back(static_cast<std::uint32_t>(neighbor));
        }
      }
    }
    return Outcome<SkeletonGraph>::success(std::move(graph));
  } catch (const cv::Exception&) {
    return skeletonFailure("opencv");
  } catch (const std::bad_alloc&) {
    return Outcome<SkeletonGraph>::failure(
        {ErrorCode::InternalError, Stage::Skeleton, "allocation"});
  } catch (...) {
    return Outcome<SkeletonGraph>::failure(
        {ErrorCode::InternalError, Stage::Skeleton, "unknown"});
  }
}

Outcome<SkeletonGraph> pruneAndMerge(SkeletonGraph graph, const CenterVein& center,
                                     const DetectionConfig& config) noexcept {
  try {
    if (!std::isfinite(config.branchMergeRadiusPx) ||
        !std::isfinite(config.minSkeletonBranchLengthNorm)) {
      return skeletonFailure("non-finite detection config");
    }

    mergeBranches(graph, config.branchMergeRadiusPx);
    const double threshold =
        config.minSkeletonBranchLengthNorm * arcLength(center.path);
    pruneShortTerminals(graph, threshold);
    reindexRowMajor(graph);
    return Outcome<SkeletonGraph>::success(std::move(graph));
  } catch (const std::bad_alloc&) {
    return Outcome<SkeletonGraph>::failure(
        {ErrorCode::InternalError, Stage::Skeleton, "allocation"});
  } catch (...) {
    return Outcome<SkeletonGraph>::failure(
        {ErrorCode::InternalError, Stage::Skeleton, "unknown"});
  }
}

}  // namespace leaf::detail
