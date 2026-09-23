#include "debug_renderer.h"

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <string>
#include <vector>

namespace leaf {
namespace {

cv::Mat rgbCanvas(ImageView image) {
  if (image.data == nullptr || image.width <= 0 || image.height <= 0) {
    return {};
  }
  const int channels = image.format == PixelFormat::Gray8   ? 1
                     : image.format == PixelFormat::RGB8    ? 3
                     : image.format == PixelFormat::RGBA8   ? 4
                                                            : 0;
  if (channels == 0) {
    return {};
  }
  const cv::Mat source(image.height, image.width, CV_MAKETYPE(CV_8U, channels),
                       const_cast<std::uint8_t*>(image.data),
                       static_cast<std::size_t>(image.stride));
  cv::Mat rgb;
  if (channels == 1) {
    cv::cvtColor(source, rgb, cv::COLOR_GRAY2RGB);
  } else if (channels == 4) {
    cv::cvtColor(source, rgb, cv::COLOR_RGBA2RGB);
  } else {
    rgb = source.clone();
  }
  return rgb;
}

void publish(DebugStage stage, const cv::Mat& rgb, IDebugObserver& observer) {
  if (rgb.empty() || !rgb.isContinuous()) {
    return;
  }
  ImageView view;
  view.data = rgb.ptr<std::uint8_t>();
  view.width = rgb.cols;
  view.height = rgb.rows;
  view.stride = static_cast<std::int32_t>(rgb.step);
  view.format = PixelFormat::RGB8;
  observer.onArtifact(DebugArtifactView{stage, view});
}

void drawPath(cv::Mat& image, const Path& path, cv::Scalar color, int thickness) {
  for (std::size_t i = 1; i < path.size(); ++i) {
    cv::line(image, cv::Point(static_cast<int>(std::lround(path[i - 1].x)),
                              static_cast<int>(std::lround(path[i - 1].y))),
             cv::Point(static_cast<int>(std::lround(path[i].x)),
                       static_cast<int>(std::lround(path[i].y))),
             color, thickness, cv::LINE_AA);
  }
}

void drawPoint(cv::Mat& image, Point point, const char* label, cv::Scalar color) {
  const cv::Point pixel(static_cast<int>(std::lround(point.x)),
                        static_cast<int>(std::lround(point.y)));
  cv::circle(image, pixel, 3, color, -1, cv::LINE_AA);
  cv::putText(image, label, pixel + cv::Point(4, -4), cv::FONT_HERSHEY_SIMPLEX, 0.35, color, 1,
              cv::LINE_AA);
}

}  // namespace

void renderDebugArtifacts(ImageView image, const AnalysisResult& result,
                          IDebugObserver& observer) noexcept {
  try {
    const cv::Mat base = rgbCanvas(image);
    if (base.empty()) {
      return;
    }

    cv::Mat gray;
    cv::cvtColor(base, gray, cv::COLOR_RGB2GRAY);
    cv::Mat grayRgb;
    cv::cvtColor(gray, grayRgb, cv::COLOR_GRAY2RGB);
    publish(DebugStage::Gray, grayRgb, observer);

    cv::Mat binary;
    cv::threshold(gray, binary, 0.0, 255.0, cv::THRESH_BINARY | cv::THRESH_OTSU);
    cv::Mat binaryRgb;
    cv::cvtColor(binary, binaryRgb, cv::COLOR_GRAY2RGB);
    publish(DebugStage::Binary, binaryRgb, observer);
    publish(DebugStage::Resized, base, observer);

    cv::Mat skeleton(base.size(), base.type(), cv::Scalar(0, 0, 0));
    drawPath(skeleton, result.centerVein.path, cv::Scalar(255, 255, 255), 1);
    drawPath(skeleton, result.secondaryVeins.leftFirst.path, cv::Scalar(80, 80, 255), 1);
    drawPath(skeleton, result.secondaryVeins.leftSecond.path, cv::Scalar(80, 255, 80), 1);
    drawPath(skeleton, result.secondaryVeins.rightFirst.path, cv::Scalar(255, 80, 80), 1);
    drawPath(skeleton, result.secondaryVeins.rightSecond.path, cv::Scalar(255, 255, 80), 1);
    publish(DebugStage::Skeleton, skeleton, observer);

    cv::Mat contour = base.clone();
    drawPath(contour, result.leaf.path, cv::Scalar(255, 180, 0), 1);
    publish(DebugStage::Contour, contour, observer);

    cv::Mat center = base.clone();
    drawPath(center, result.centerVein.path, cv::Scalar(255, 40, 40), 2);
    drawPoint(center, result.centerVein.base, "base", cv::Scalar(255, 40, 40));
    drawPoint(center, result.centerVein.apex, "apex", cv::Scalar(40, 40, 255));
    publish(DebugStage::CenterVein, center, observer);

    cv::Mat veins = center.clone();
    drawPath(veins, result.secondaryVeins.leftFirst.path, cv::Scalar(40, 180, 40), 1);
    drawPath(veins, result.secondaryVeins.leftSecond.path, cv::Scalar(40, 220, 180), 1);
    drawPath(veins, result.secondaryVeins.rightFirst.path, cv::Scalar(220, 180, 40), 1);
    drawPath(veins, result.secondaryVeins.rightSecond.path, cv::Scalar(180, 40, 220), 1);
    publish(DebugStage::SecondaryVeins, veins, observer);

    cv::Mat keys = veins.clone();
    const auto mark = [&keys](const NormalizedPoint& point, const char* label) {
      drawPoint(keys, point.image, label, cv::Scalar(255, 255, 0));
    };
    mark(result.keypoints.a1, "a1");
    mark(result.keypoints.a2, "a2");
    mark(result.keypoints.b1, "b1");
    mark(result.keypoints.b2, "b2");
    mark(result.keypoints.c1, "c1");
    mark(result.keypoints.c2, "c2");
    mark(result.keypoints.d1, "d1");
    mark(result.keypoints.d2, "d2");
    mark(result.keypoints.e, "e");
    mark(result.keypoints.f, "f");
    mark(result.keypoints.g1, "g1");
    mark(result.keypoints.g2, "g2");
    const Point origin = result.coordinateSystem.origin;
    const double axis = std::max(20.0, result.coordinateSystem.scale * 0.15);
    cv::arrowedLine(keys,
                    cv::Point(static_cast<int>(std::lround(origin.x)),
                              static_cast<int>(std::lround(origin.y))),
                    cv::Point(static_cast<int>(std::lround(origin.x + result.coordinateSystem.xAxis.x * axis)),
                              static_cast<int>(std::lround(origin.y + result.coordinateSystem.xAxis.y * axis))),
                    cv::Scalar(0, 255, 255), 1, cv::LINE_AA);
    cv::arrowedLine(keys,
                    cv::Point(static_cast<int>(std::lround(origin.x)),
                              static_cast<int>(std::lround(origin.y))),
                    cv::Point(static_cast<int>(std::lround(origin.x + result.coordinateSystem.yAxis.x * axis)),
                              static_cast<int>(std::lround(origin.y + result.coordinateSystem.yAxis.y * axis))),
                    cv::Scalar(255, 0, 255), 1, cv::LINE_AA);
    publish(DebugStage::Keypoints, keys, observer);

    cv::Mat finalImage = keys.clone();
    const auto& left = result.measurements.left;
    const auto& right = result.measurements.right;
    const std::string text =
        "M1 " + std::to_string(left.m1) + " / " + std::to_string(right.m1) + "  M2 " +
        std::to_string(left.m2) + " / " + std::to_string(right.m2) + "  M3 " +
        std::to_string(left.m3) + " / " + std::to_string(right.m3) + "  M4 " +
        std::to_string(left.m4) + " / " + std::to_string(right.m4) + "  M5 " +
        std::to_string(left.m5) + " / " + std::to_string(right.m5);
    cv::putText(finalImage, text, cv::Point(8, 16), cv::FONT_HERSHEY_SIMPLEX, 0.35,
                cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
    publish(DebugStage::Final, finalImage, observer);
  } catch (...) {
  }
}

}  // namespace leaf
