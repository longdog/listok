#include "secondary_vein_detector.h"

#include "../../geometry/contour_arc.h"
#include "../../geometry/polyline.h"

#include <new>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace leaf::detail {
namespace {

constexpr double kEps = 1e-9;

struct Projection {
  double distance = std::numeric_limits<double>::infinity();
  double arc = 0.0;
};

double dist(Point a, Point b) {
  const double dx = a.x - b.x;
  const double dy = a.y - b.y;
  return std::sqrt(dx * dx + dy * dy);
}

bool finitePoint(Point p) { return std::isfinite(p.x) && std::isfinite(p.y); }

Projection projectPoint(const Path& path, Point p) {
  Projection best;
  if (path.empty() || !finitePoint(p)) {
    return best;
  }
  if (path.size() == 1) {
    best.distance = dist(path.front(), p);
    best.arc = 0.0;
    return best;
  }
  double acc = 0.0;
  for (std::size_t i = 1; i < path.size(); ++i) {
    const Point a = path[i - 1];
    const Point b = path[i];
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;
    const double segLen = std::sqrt(len2);
    double t = 0.0;
    if (len2 > 0.0) {
      t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
      if (t < 0.0) {
        t = 0.0;
      } else if (t > 1.0) {
        t = 1.0;
      }
    }
    const Point q{a.x + t * dx, a.y + t * dy};
    const double d = dist(p, q);
    if (d < best.distance) {
      best.distance = d;
      best.arc = acc + t * segLen;
    }
    acc += segLen;
  }
  return best;
}

double distanceToContour(const Path& contour, Point p) {
  if (contour.empty() || !finitePoint(p)) {
    return std::numeric_limits<double>::infinity();
  }
  auto consider = [&](Point a, Point b, double& best) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;
    double t = 0.0;
    if (len2 > 0.0) {
      t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
      if (t < 0.0) {
        t = 0.0;
      } else if (t > 1.0) {
        t = 1.0;
      }
    }
    const Point q{a.x + t * dx, a.y + t * dy};
    best = std::min(best, dist(p, q));
  };

  double best = std::numeric_limits<double>::infinity();
  for (std::size_t i = 1; i < contour.size(); ++i) {
    consider(contour[i - 1], contour[i], best);
  }
  if (contour.size() >= 2 && dist(contour.front(), contour.back()) > kEps) {
    consider(contour.back(), contour.front(), best);
  }
  return best;
}

struct Candidate {
  Vein vein;
  double centerArc = 0.0;
  double rankY = 0.0;
  double rankX = 0.0;
  std::uint32_t id = 0;
  int side = 0;
};

bool betterRank(const Candidate& a, const Candidate& b) {
  if (a.vein.confidence != b.vein.confidence) {
    return a.vein.confidence > b.vein.confidence;
  }
  if (a.vein.arcLength != b.vein.arcLength) {
    return a.vein.arcLength > b.vein.arcLength;
  }
  if (a.rankY != b.rankY) {
    return a.rankY < b.rankY;
  }
  if (a.rankX != b.rankX) {
    return a.rankX < b.rankX;
  }
  return a.id < b.id;
}

Outcome<SecondaryVeins> fail(ErrorCode code, const char* msg) {
  return Outcome<SecondaryVeins>::failure({code, Stage::SecondaryVeins, msg});
}

}  // namespace

Outcome<SecondaryVeins> detectSecondaryVeins(const SkeletonGraph& graph, const LeafContour& contour,
                                             const CenterVein& center, const CoordinateSystem& cs,
                                             const DetectionConfig& config) noexcept {
  try {
    if (!(cs.scale > 0.0) || !std::isfinite(cs.scale) || center.path.size() < 2) {
      return fail(ErrorCode::InternalError, "invalid center or scale");
    }
    const double centerLength = arcLength(center.path);
    if (!(centerLength > 0.0) || !std::isfinite(centerLength)) {
      return fail(ErrorCode::InternalError, "invalid center length");
    }

    const Point tangent{center.apex.x - center.base.x, center.apex.y - center.base.y};
    if (!finitePoint(tangent) || (tangent.x == 0.0 && tangent.y == 0.0)) {
      return fail(ErrorCode::InternalError, "invalid center tangent");
    }

    const auto& nodes = graph.nodes;
    const std::size_t n = nodes.size();
    std::unordered_map<std::uint32_t, std::size_t> indexOf;
    indexOf.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
      indexOf.emplace(nodes[i].id, i);
    }

    auto resolve = [&](std::uint32_t edge) -> std::size_t {
      const auto it = indexOf.find(edge);
      if (it != indexOf.end()) {
        return it->second;
      }
      if (edge < n) {
        return static_cast<std::size_t>(edge);
      }
      return n;
    };

    std::vector<std::vector<std::size_t>> adj(n);
    for (std::size_t i = 0; i < n; ++i) {
      for (std::uint32_t edge : nodes[i].edges) {
        const std::size_t j = resolve(edge);
        if (j >= n || j == i) {
          continue;
        }
        adj[i].push_back(j);
        adj[j].push_back(i);
      }
    }
    for (auto& nb : adj) {
      std::sort(nb.begin(), nb.end());
      nb.erase(std::unique(nb.begin(), nb.end()), nb.end());
    }

    std::vector<char> isCenter(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
      if (!finitePoint(nodes[i].point)) {
        continue;
      }
      const Projection proj = projectPoint(center.path, nodes[i].point);
      if (proj.distance <= config.branchMergeRadiusPx) {
        isCenter[i] = 1;
      }
    }

    std::vector<Candidate> candidates;
    for (std::size_t junction = 0; junction < n; ++junction) {
      if (!isCenter[junction]) {
        continue;
      }
      for (std::size_t firstOff : adj[junction]) {
        if (isCenter[firstOff]) {
          continue;
        }
        Path path;
        path.push_back(nodes[firstOff].point);
        std::size_t prev = junction;
        std::size_t cur = firstOff;
        bool walkOk = true;
        while (true) {
          std::vector<std::size_t> next;
          for (std::size_t nb : adj[cur]) {
            if (nb != prev) {
              next.push_back(nb);
            }
          }
          if (next.size() != 1) {
            break;
          }
          prev = cur;
          cur = next[0];
          if (isCenter[cur] || !finitePoint(nodes[cur].point)) {
            walkOk = false;
            break;
          }
          path.push_back(nodes[cur].point);
        }
        if (!walkOk || path.size() < 2) {
          continue;
        }

        const Point attachment = path.front();
        const Point endpoint = path.back();
        const double length = arcLength(path);
        if (!(length > 0.0) || !std::isfinite(length)) {
          continue;
        }
        const double normLength = length / cs.scale;
        if (!std::isfinite(normLength) || normLength < config.minSecondaryVeinLengthNorm) {
          continue;
        }

        const Point direction{endpoint.x - attachment.x, endpoint.y - attachment.y};
        if (direction.x == 0.0 && direction.y == 0.0) {
          continue;
        }
        const double angle = unorientedAngleDegrees(direction, tangent);
        if (!std::isfinite(angle) || angle < config.minSecondaryAngleDeg || angle > config.maxSecondaryAngleDeg) {
          continue;
        }

        const double endX = cs.imageToNormalized(endpoint).x;
        if (!std::isfinite(endX) || std::abs(endX) <= kEps) {
          continue;
        }
        const int side = endX < 0.0 ? -1 : 1;

        const double contourDist = distanceToContour(contour.path, endpoint);
        if (!std::isfinite(contourDist) || contourDist > config.endpointContourSnapPx + kEps) {
          continue;
        }

        const Projection onCenter = projectPoint(center.path, attachment);
        if (!std::isfinite(onCenter.arc)) {
          continue;
        }

        double confidence = normLength;
        if (confidence < 0.0) {
          confidence = 0.0;
        } else if (confidence > 1.0) {
          confidence = 1.0;
        }
        if (!std::isfinite(confidence) || confidence < config.minimumStageConfidence) {
          continue;
        }

        Vein vein;
        vein.path = std::move(path);
        vein.attachment = attachment;
        vein.endpoint = endpoint;
        vein.arcLength = length;
        vein.confidence = confidence;

        Candidate candidate;
        candidate.vein = std::move(vein);
        candidate.centerArc = onCenter.arc;
        candidate.rankY = attachment.y;
        candidate.rankX = attachment.x;
        candidate.id = nodes[firstOff].id;
        candidate.side = side;
        candidates.push_back(std::move(candidate));
      }
    }

    std::sort(candidates.begin(), candidates.end(), betterRank);

    std::vector<Candidate> kept;
    kept.reserve(candidates.size());
    for (const Candidate& candidate : candidates) {
      bool tooClose = false;
      for (const Candidate& prior : kept) {
        if (prior.side != candidate.side) {
          continue;
        }
        const double separation = std::abs(prior.centerArc - candidate.centerArc) / centerLength;
        if (separation < config.minAttachmentSeparationNorm) {
          tooClose = true;
          break;
        }
      }
      if (!tooClose) {
        kept.push_back(candidate);
      }
    }

    if (kept.size() >= 2 &&
        (kept[0].vein.confidence - kept[1].vein.confidence) < config.ambiguityScoreDelta) {
      return fail(ErrorCode::AmbiguousVeins, "ambiguous secondary veins");
    }

    std::vector<Candidate> left;
    std::vector<Candidate> right;
    for (const Candidate& candidate : kept) {
      if (candidate.side < 0) {
        left.push_back(candidate);
      } else {
        right.push_back(candidate);
      }
    }
    if (left.size() < 2 || right.size() < 2) {
      return fail(ErrorCode::SecondaryVeinNotFound, "secondary vein not found");
    }

    auto byArc = [](const Candidate& a, const Candidate& b) {
      if (a.centerArc != b.centerArc) {
        return a.centerArc < b.centerArc;
      }
      return betterRank(a, b);
    };
    std::sort(left.begin(), left.end(), byArc);
    std::sort(right.begin(), right.end(), byArc);

    // Keep the two extreme ordered veins when a side has extras: the earliest and latest
    // by center arc among the already rank-filtered set. Tests provide exactly two.
    if (left.size() > 2) {
      left = {left.front(), left.back()};
    }
    if (right.size() > 2) {
      right = {right.front(), right.back()};
    }
    if (!(left[0].centerArc < left[1].centerArc) || !(right[0].centerArc < right[1].centerArc)) {
      return fail(ErrorCode::SecondaryVeinNotFound, "secondary vein attachment order");
    }

    SecondaryVeins veins;
    veins.leftFirst = std::move(left[0].vein);
    veins.leftSecond = std::move(left[1].vein);
    veins.rightFirst = std::move(right[0].vein);
    veins.rightSecond = std::move(right[1].vein);
    veins.confidence = std::min(std::min(veins.leftFirst.confidence, veins.leftSecond.confidence),
                                std::min(veins.rightFirst.confidence, veins.rightSecond.confidence));
    if (!std::isfinite(veins.confidence)) {
      return fail(ErrorCode::InternalError, "non-finite vein confidence");
    }
    return Outcome<SecondaryVeins>::success(std::move(veins));
  } catch (const std::bad_alloc&) {
    return fail(ErrorCode::InternalError, "out of memory");
  } catch (...) {
    return fail(ErrorCode::InternalError, "internal error");
  }
}

}  // namespace leaf::detail
