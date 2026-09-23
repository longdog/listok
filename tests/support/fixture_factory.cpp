#include "fixture_factory.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <sstream>

namespace {

leaf::NormalizedPoint makeKeypoint(const leaf::CoordinateSystem& cs, double imageX,
                                   double imageY) {
  const leaf::Point image{imageX, imageY};
  return {image, cs.imageToNormalized(image)};
}

double polylineLength(const leaf::Path& path) noexcept {
  double total = 0.0;
  for (std::size_t i = 1; i < path.size(); ++i) {
    total += std::hypot(path[i].x - path[i - 1].x, path[i].y - path[i - 1].y);
  }
  return total;
}

leaf::Point last(const leaf::Path& path) noexcept { return path.back(); }

}  // namespace

MeasurementDomain canonicalMeasurementDomain() {
  MeasurementDomain domain{};

  domain.center.path = {{0, 0}, {0, 2}, {0, 4}, {0, 4.5}, {0, 10}};
  domain.center.base = {0, 0};
  domain.center.apex = {0, 10};
  domain.center.confidence = 1.0;

  domain.cs = leaf::CoordinateSystem{{0, 0}, {1, 0}, {0, 1}, 10.0};

  const double invSqrt2 = 1.0 / std::sqrt(2.0);
  const double sin60 = std::sqrt(3.0) / 2.0;
  const double cos60 = 0.5;

  domain.veins.leftFirst.attachment = {0, 2};
  domain.veins.leftSecond.attachment = {0, 4};
  domain.veins.rightFirst.attachment = {0, 2};
  domain.veins.rightSecond.attachment = {0, 4.5};

  domain.veins.leftSecond.path = {
      {0, 4},
      {-0.05 * invSqrt2, 4.0 + 0.05 * invSqrt2},
      {-0.1 * invSqrt2, 4.0 + 0.1 * invSqrt2},
      {-0.2 * invSqrt2, 4.0 + 0.2 * invSqrt2},
  };
  const double leftRemaining = 3.0 - polylineLength(domain.veins.leftSecond.path);
  domain.veins.leftSecond.path.push_back({last(domain.veins.leftSecond.path).x - leftRemaining,
                                          last(domain.veins.leftSecond.path).y});
  domain.veins.leftSecond.endpoint = last(domain.veins.leftSecond.path);
  domain.veins.leftSecond.arcLength = 3.0;
  domain.veins.leftSecond.confidence = 1.0;

  const leaf::Point leftFirstEndpoint{domain.veins.leftSecond.endpoint.x,
                                      domain.veins.leftSecond.endpoint.y - 2.5};
  domain.veins.leftFirst.path = {{0, 2}, leftFirstEndpoint};
  domain.veins.leftFirst.endpoint = leftFirstEndpoint;
  domain.veins.leftFirst.arcLength = polylineLength(domain.veins.leftFirst.path);
  domain.veins.leftFirst.confidence = 1.0;

  domain.veins.rightSecond.path = {
      {0, 4.5},
      {0.05 * sin60, 4.5 + 0.05 * cos60},
      {0.1 * sin60, 4.5 + 0.1 * cos60},
      {0.2 * sin60, 4.5 + 0.2 * cos60},
  };
  const double rightRemaining = 4.0 - polylineLength(domain.veins.rightSecond.path);
  domain.veins.rightSecond.path.push_back({last(domain.veins.rightSecond.path).x,
                                           last(domain.veins.rightSecond.path).y + rightRemaining});
  domain.veins.rightSecond.endpoint = last(domain.veins.rightSecond.path);
  domain.veins.rightSecond.arcLength = 4.0;
  domain.veins.rightSecond.confidence = 1.0;

  const leaf::Point rightFirstEndpoint{domain.veins.rightSecond.endpoint.x + 3.0,
                                       domain.veins.rightSecond.endpoint.y};
  domain.veins.rightFirst.path = {{0, 2}, rightFirstEndpoint};
  domain.veins.rightFirst.endpoint = rightFirstEndpoint;
  domain.veins.rightFirst.arcLength = polylineLength(domain.veins.rightFirst.path);
  domain.veins.rightFirst.confidence = 1.0;
  domain.veins.confidence = 1.0;

  const leaf::Point leftA = domain.veins.leftFirst.endpoint;
  const leaf::Point leftB = domain.veins.leftSecond.endpoint;
  const leaf::Point rightA = domain.veins.rightFirst.endpoint;
  const leaf::Point rightB = domain.veins.rightSecond.endpoint;

  domain.contour.path = {{-4, 5}, {-4, 10}, {0, 11}, {5, 10}, rightA, rightB, {5, 5},
                         {5, 0},  {0, -1},  {-4, 0}, leftA,  leftB,  {-4, 5}};
  domain.contour.confidence = 1.0;

  domain.keypoints.a1 = makeKeypoint(domain.cs, 0, 2);
  domain.keypoints.b1 = makeKeypoint(domain.cs, 0, 4);
  domain.keypoints.c1 = makeKeypoint(domain.cs, leftA.x, leftA.y);
  domain.keypoints.d1 = makeKeypoint(domain.cs, leftB.x, leftB.y);
  domain.keypoints.a2 = makeKeypoint(domain.cs, 0, 2);
  domain.keypoints.b2 = makeKeypoint(domain.cs, 0, 4.5);
  domain.keypoints.c2 = makeKeypoint(domain.cs, rightA.x, rightA.y);
  domain.keypoints.d2 = makeKeypoint(domain.cs, rightB.x, rightB.y);
  domain.keypoints.e = makeKeypoint(domain.cs, 0, 10);
  domain.keypoints.f = makeKeypoint(domain.cs, 0, 5);
  domain.keypoints.g1 = makeKeypoint(domain.cs, -4, 5);
  domain.keypoints.g2 = makeKeypoint(domain.cs, 5, 5);
  domain.keypoints.confidence = 1.0;

  return domain;
}

namespace {

std::string sourcePath(const char* relative) {
  return std::string(CMAKE_SOURCE_DIR) + "/" + relative;
}

bool readNetpbm(const std::string& path, std::string& magic, int& width, int& height,
                std::vector<std::uint8_t>& pixels) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return false;
  }
  auto nextToken = [&input](std::string& token) {
    token.clear();
    char character = 0;
    while (input.get(character)) {
      if (character == '#') {
        std::string ignored;
        std::getline(input, ignored);
        continue;
      }
      if (std::isspace(static_cast<unsigned char>(character))) {
        continue;
      }
      token.push_back(character);
      while (input.get(character)) {
        if (std::isspace(static_cast<unsigned char>(character))) {
          return true;
        }
        token.push_back(character);
      }
      return !token.empty();
    }
    return !token.empty();
  };

  std::string token;
  if (!nextToken(magic) || (magic != "P5" && magic != "P6") || !nextToken(token)) {
    return false;
  }
  width = std::stoi(token);
  if (!nextToken(token)) {
    return false;
  }
  height = std::stoi(token);
  if (!nextToken(token) || std::stoi(token) != 255 || width <= 0 || height <= 0) {
    return false;
  }
  const std::size_t channels = magic == "P6" ? 3u : 1u;
  pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * channels);
  input.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
  return input.gcount() == static_cast<std::streamsize>(pixels.size());
}

leaf::Point pointFrom(const nlohmann::json& json) {
  return {json.at("x").get<double>(), json.at("y").get<double>()};
}

leaf::Path pathFrom(const nlohmann::json& json) {
  leaf::Path path;
  path.reserve(json.size());
  for (const auto& item : json) {
    path.push_back(pointFrom(item));
  }
  return path;
}

leaf::SideMeasurements sideFrom(const nlohmann::json& json) {
  leaf::SideMeasurements side;
  side.m1 = json.at("m1").get<double>();
  side.m2 = json.at("m2").get<double>();
  side.m3 = json.at("m3").get<double>();
  side.m4 = json.at("m4").get<double>();
  side.m5 = json.at("m5").get<double>();
  return side;
}

}  // namespace

SyntheticLeafFixture makeSyntheticLeafV1() {
  SyntheticLeafFixture fixture;
  std::string magic;
  std::vector<std::uint8_t> pixels;
  int width = 0;
  int height = 0;
  if (!readNetpbm(sourcePath("tests/fixtures/synthetic/leaf_v1.ppm"), magic, width, height, pixels) ||
      magic != "P6") {
    return fixture;
  }
  fixture.width = width;
  fixture.height = height;
  fixture.rgb = std::move(pixels);
  int maskWidth = 0;
  int maskHeight = 0;
  if (!readNetpbm(sourcePath("tests/fixtures/synthetic/leaf_v1.mask.pgm"), magic, maskWidth, maskHeight,
                  fixture.mask) ||
      magic != "P5" || maskWidth != width || maskHeight != height) {
    fixture.rgb.clear();
    fixture.mask.clear();
    fixture.width = 0;
    fixture.height = 0;
    return fixture;
  }
  fixture.groundTruthPath = sourcePath("tests/fixtures/synthetic/leaf_v1.ground-truth.json");
  return fixture;
}

leaf::Outcome<GroundTruth> readGroundTruth(const std::string& path) {
  try {
    std::ifstream input(path);
    if (!input) {
      return leaf::Outcome<GroundTruth>::failure(
          {leaf::ErrorCode::InvalidConfigJson, leaf::Stage::Input, "ground truth missing"});
    }
    const auto json = nlohmann::json::parse(input);
    GroundTruth truth;
    truth.algorithmVersion = json.at("algorithmVersion").get<std::string>();
    truth.contour = pathFrom(json.at("contour"));
    truth.center = pathFrom(json.at("center"));
    const auto& points = json.at("keypoints");
    truth.a1 = pointFrom(points.at("a1"));
    truth.a2 = pointFrom(points.at("a2"));
    truth.b1 = pointFrom(points.at("b1"));
    truth.b2 = pointFrom(points.at("b2"));
    truth.c1 = pointFrom(points.at("c1"));
    truth.c2 = pointFrom(points.at("c2"));
    truth.d1 = pointFrom(points.at("d1"));
    truth.d2 = pointFrom(points.at("d2"));
    truth.e = pointFrom(points.at("e"));
    truth.f = pointFrom(points.at("f"));
    truth.g1 = pointFrom(points.at("g1"));
    truth.g2 = pointFrom(points.at("g2"));
    const auto& secondary = json.at("secondary");
    truth.leftFirst = pathFrom(secondary.at("leftFirst"));
    truth.leftSecond = pathFrom(secondary.at("leftSecond"));
    truth.rightFirst = pathFrom(secondary.at("rightFirst"));
    truth.rightSecond = pathFrom(secondary.at("rightSecond"));
    truth.left = sideFrom(json.at("measurements").at("left"));
    truth.right = sideFrom(json.at("measurements").at("right"));
    const auto& asymmetry = json.at("asymmetry");
    truth.asymmetry.m1 = asymmetry.at("m1").get<double>();
    truth.asymmetry.m2 = asymmetry.at("m2").get<double>();
    truth.asymmetry.m3 = asymmetry.at("m3").get<double>();
    truth.asymmetry.m4 = asymmetry.at("m4").get<double>();
    truth.asymmetry.m5 = asymmetry.at("m5").get<double>();
    truth.k = json.at("k").get<double>();
    truth.score = json.at("score").get<int>();
    return leaf::Outcome<GroundTruth>::success(std::move(truth));
  } catch (const std::bad_alloc&) {
    return leaf::Outcome<GroundTruth>::failure(
        {leaf::ErrorCode::InternalError, leaf::Stage::Input, "allocation"});
  } catch (...) {
    return leaf::Outcome<GroundTruth>::failure(
        {leaf::ErrorCode::InvalidConfigJson, leaf::Stage::Input, "ground truth"});
  }
}
