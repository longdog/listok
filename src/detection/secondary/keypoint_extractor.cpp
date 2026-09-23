#include "keypoint_extractor.h"

#include "../../geometry/intersection.h"
#include "../../geometry/polyline.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

namespace leaf::detail {
namespace {

constexpr double kEps = 1e-9;

double dist(Point a, Point b) {
  const double dx = a.x - b.x;
  const double dy = a.y - b.y;
  return std::sqrt(dx * dx + dy * dy);
}

struct Projection {
  double arc = 0.0;
  bool ok = false;
};

Projection projectArc(const Path& path, Point p) {
  Projection best;
  if (path.empty() || !std::isfinite(p.x) || !std::isfinite(p.y)) {
    return best;
  }
  if (path.size() == 1) {
    best.arc = 0.0;
    best.ok = true;
    return best;
  }
  double acc = 0.0;
  double bestDist = 0.0;
  bool any = false;
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
    if (!any || d < bestDist) {
      bestDist = d;
      best.arc = acc + t * segLen;
      best.ok = true;
      any = true;
    }
    acc += segLen;
  }
  return best;
}

Outcome<LeafKeypoints> fail(ErrorCode code, const char* msg) {
  return Outcome<LeafKeypoints>::failure({code, Stage::Keypoints, msg});
}

NormalizedPoint makePoint(Point image, const CoordinateSystem& cs) {
  return NormalizedPoint{image, cs.imageToNormalized(image)};
}

bool finiteUnit(double v) { return std::isfinite(v) && v >= 0.0 && v <= 1.0; }

}  // namespace

Outcome<LeafKeypoints> extractKeypoints(const LeafContour& contour, const CenterVein& center,
                                        const SecondaryVeins& veins, const CoordinateSystem& cs) noexcept {
  try {
    if (!(cs.scale > 0.0) || !std::isfinite(cs.scale) || center.path.size() < 2) {
      return fail(ErrorCode::InternalError, "invalid center or scale");
    }

    const Projection leftFirstArc = projectArc(center.path, veins.leftFirst.attachment);
    const Projection leftSecondArc = projectArc(center.path, veins.leftSecond.attachment);
    const Projection rightFirstArc = projectArc(center.path, veins.rightFirst.attachment);
    const Projection rightSecondArc = projectArc(center.path, veins.rightSecond.attachment);
    if (!leftFirstArc.ok || !leftSecondArc.ok || !rightFirstArc.ok || !rightSecondArc.ok) {
      return fail(ErrorCode::InternalError, "attachment projection failed");
    }
    if (!(leftFirstArc.arc < leftSecondArc.arc) || !(rightFirstArc.arc < rightSecondArc.arc)) {
      return fail(ErrorCode::InvalidKeypoints, "attachment arc order");
    }

    const double left1x = cs.imageToNormalized(veins.leftFirst.endpoint).x;
    const double left2x = cs.imageToNormalized(veins.leftSecond.endpoint).x;
    const double right1x = cs.imageToNormalized(veins.rightFirst.endpoint).x;
    const double right2x = cs.imageToNormalized(veins.rightSecond.endpoint).x;
    if (!(left1x < 0.0) || !(left2x < 0.0) || !(right1x > 0.0) || !(right2x > 0.0)) {
      return fail(ErrorCode::InvalidKeypoints, "endpoint side");
    }

    const double centerLen = arcLength(center.path);
    if (!(centerLen > 0.0) || !std::isfinite(centerLen)) {
      return fail(ErrorCode::InternalError, "invalid center length");
    }
    const auto mid = pointAtArc(center.path, 0.5 * centerLen);
    if (!mid.hasValue() || mid.value() == nullptr) {
      return fail(ErrorCode::InternalError, "center midpoint");
    }

    LeafKeypoints kp;
    kp.a1 = makePoint(veins.leftFirst.attachment, cs);
    kp.c1 = makePoint(veins.leftFirst.endpoint, cs);
    kp.b1 = makePoint(veins.leftSecond.attachment, cs);
    kp.d1 = makePoint(veins.leftSecond.endpoint, cs);
    kp.a2 = makePoint(veins.rightFirst.attachment, cs);
    kp.c2 = makePoint(veins.rightFirst.endpoint, cs);
    kp.b2 = makePoint(veins.rightSecond.attachment, cs);
    kp.d2 = makePoint(veins.rightSecond.endpoint, cs);
    kp.e = makePoint(center.apex, cs);
    kp.f = makePoint(*mid.value(), cs);

    Point g1 = {};
    Point g2 = {};
    bool assigned = false;
    const auto hits = contourRayHits(contour.path, kp.f.image, cs.xAxis);
    if (hits.hasValue() && hits.value() != nullptr) {
      const Point h0 = hits.value()->first;
      const Point h1 = hits.value()->second;
      const double x0 = cs.imageToNormalized(h0).x;
      const double x1 = cs.imageToNormalized(h1).x;
      if (x0 < -kEps && x1 > kEps) {
        g1 = h0;
        g2 = h1;
        assigned = true;
      } else if (x1 < -kEps && x0 > kEps) {
        g1 = h1;
        g2 = h0;
        assigned = true;
      }
    }
    if (!assigned) {
      bool gotLeft = false;
      bool gotRight = false;
      for (const Point& corner : contour.path) {
        const double nx = cs.imageToNormalized(corner).x;
        if (!gotLeft && nx < 0.0) {
          g1 = corner;
          gotLeft = true;
        } else if (!gotRight && nx > 0.0) {
          g2 = corner;
          gotRight = true;
        }
      }
      if (!gotLeft || !gotRight) {
        return fail(ErrorCode::InvalidKeypoints, "contour width points");
      }
    }

    kp.g1 = makePoint(g1, cs);
    kp.g2 = makePoint(g2, cs);

    kp.confidence = std::min(std::min(veins.leftFirst.confidence, veins.leftSecond.confidence),
                             std::min(veins.rightFirst.confidence, veins.rightSecond.confidence));
    if (!finiteUnit(kp.confidence)) {
      return fail(ErrorCode::InvalidKeypoints, "vein confidence");
    }
    return Outcome<LeafKeypoints>::success(std::move(kp));
  } catch (const std::bad_alloc&) {
    return Outcome<LeafKeypoints>::failure({ErrorCode::InternalError, Stage::Keypoints, "out of memory"});
  } catch (...) {
    return Outcome<LeafKeypoints>::failure({ErrorCode::InternalError, Stage::Keypoints, "internal error"});
  }
}

}  // namespace leaf::detail
