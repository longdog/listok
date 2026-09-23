#include <leaf/json.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <stdexcept>
#include <utility>

namespace leaf {
namespace {

using Json = nlohmann::ordered_json;

Outcome<std::string> serializationError(const char* message) noexcept {
  return Outcome<std::string>::failure(
      {ErrorCode::SerializationFailed, Stage::Serialization, message});
}

const char* errorCodeName(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::None:
      return "None";
    case ErrorCode::InvalidImage:
      return "InvalidImage";
    case ErrorCode::UnsupportedPixelFormat:
      return "UnsupportedPixelFormat";
    case ErrorCode::InvalidConfigJson:
      return "InvalidConfigJson";
    case ErrorCode::UnknownConfigField:
      return "UnknownConfigField";
    case ErrorCode::InvalidConfigValue:
      return "InvalidConfigValue";
    case ErrorCode::InsufficientContrast:
      return "InsufficientContrast";
    case ErrorCode::InsufficientLighting:
      return "InsufficientLighting";
    case ErrorCode::PreprocessFailed:
      return "PreprocessFailed";
    case ErrorCode::LeafNotFound:
      return "LeafNotFound";
    case ErrorCode::LeafTooSmall:
      return "LeafTooSmall";
    case ErrorCode::LeafOutsideFrame:
      return "LeafOutsideFrame";
    case ErrorCode::AmbiguousLeafContour:
      return "AmbiguousLeafContour";
    case ErrorCode::CentralVeinNotFound:
      return "CentralVeinNotFound";
    case ErrorCode::AmbiguousCentralVein:
      return "AmbiguousCentralVein";
    case ErrorCode::InvalidCoordinateSystem:
      return "InvalidCoordinateSystem";
    case ErrorCode::SkeletonizationFailed:
      return "SkeletonizationFailed";
    case ErrorCode::SecondaryVeinNotFound:
      return "SecondaryVeinNotFound";
    case ErrorCode::AmbiguousVeins:
      return "AmbiguousVeins";
    case ErrorCode::InvalidKeypoints:
      return "InvalidKeypoints";
    case ErrorCode::InvalidMeasurements:
      return "InvalidMeasurements";
    case ErrorCode::ZeroAsymmetryDenominator:
      return "ZeroAsymmetryDenominator";
    case ErrorCode::InvalidScoreRanges:
      return "InvalidScoreRanges";
    case ErrorCode::SerializationFailed:
      return "SerializationFailed";
    case ErrorCode::InternalError:
      return "InternalError";
  }
  return "InternalError";
}

const char* stageName(Stage stage) noexcept {
  switch (stage) {
    case Stage::Input:
      return "Input";
    case Stage::Config:
      return "Config";
    case Stage::Preprocess:
      return "Preprocess";
    case Stage::LeafContour:
      return "LeafContour";
    case Stage::CenterVein:
      return "CenterVein";
    case Stage::CoordinateSystem:
      return "CoordinateSystem";
    case Stage::Skeleton:
      return "Skeleton";
    case Stage::SecondaryVeins:
      return "SecondaryVeins";
    case Stage::Keypoints:
      return "Keypoints";
    case Stage::Measurements:
      return "Measurements";
    case Stage::Statistics:
      return "Statistics";
    case Stage::Score:
      return "Score";
    case Stage::Quality:
      return "Quality";
    case Stage::Serialization:
      return "Serialization";
    case Stage::Internal:
      return "Internal";
  }
  return "Internal";
}

const char* qualityIssueName(QualityIssue issue) noexcept {
  switch (issue) {
    case QualityIssue::LowLeafConfidence:
      return "LowLeafConfidence";
    case QualityIssue::LowCenterVeinConfidence:
      return "LowCenterVeinConfidence";
    case QualityIssue::LowSecondaryVeinConfidence:
      return "LowSecondaryVeinConfidence";
    case QualityIssue::LowKeypointConfidence:
      return "LowKeypointConfidence";
    case QualityIssue::LowMeasurementConfidence:
      return "LowMeasurementConfidence";
    case QualityIssue::MarginalContrast:
      return "MarginalContrast";
    case QualityIssue::MarginalLighting:
      return "MarginalLighting";
  }
  return "LowLeafConfidence";
}

bool finiteNumber(double value) noexcept { return std::isfinite(value); }

bool finitePoint(Point point) noexcept { return finiteNumber(point.x) && finiteNumber(point.y); }

bool finitePath(const Path& path) noexcept {
  for (const Point& point : path) {
    if (!finitePoint(point)) {
      return false;
    }
  }
  return true;
}

bool finiteRect(Rect rect) noexcept {
  return finiteNumber(rect.x) && finiteNumber(rect.y) && finiteNumber(rect.width) &&
         finiteNumber(rect.height);
}

bool finiteNormalized(const NormalizedPoint& point) noexcept {
  return finitePoint(point.image) && finitePoint(point.normalized);
}

bool finiteVein(const Vein& vein) noexcept {
  return finitePath(vein.path) && finitePoint(vein.attachment) && finitePoint(vein.endpoint) &&
         finiteNumber(vein.arcLength) && finiteNumber(vein.confidence);
}

bool finiteResult(const AnalysisResult& result) noexcept {
  const Versions& versions = result.versions;
  if (versions.modelVersion.has_value() && versions.modelVersion->empty()) {
    // Empty model version is still a finite string. Nothing to reject.
  }
  if (!finitePoint(result.coordinateSystem.origin) || !finitePoint(result.coordinateSystem.xAxis) ||
      !finitePoint(result.coordinateSystem.yAxis) || !finiteNumber(result.coordinateSystem.scale) ||
      !finitePath(result.leaf.path) || !finiteNumber(result.leaf.area) ||
      !finiteRect(result.leaf.boundingBox) || !finiteNumber(result.leaf.confidence) ||
      !finitePath(result.centerVein.path) || !finitePoint(result.centerVein.base) ||
      !finitePoint(result.centerVein.apex) || !finiteNumber(result.centerVein.confidence) ||
      !finiteVein(result.secondaryVeins.leftFirst) || !finiteVein(result.secondaryVeins.leftSecond) ||
      !finiteVein(result.secondaryVeins.rightFirst) || !finiteVein(result.secondaryVeins.rightSecond) ||
      !finiteNumber(result.secondaryVeins.confidence)) {
    return false;
  }
  const NormalizedPoint points[] = {
      result.keypoints.a1, result.keypoints.a2, result.keypoints.b1, result.keypoints.b2,
      result.keypoints.c1, result.keypoints.c2, result.keypoints.d1, result.keypoints.d2,
      result.keypoints.e,  result.keypoints.f,  result.keypoints.g1, result.keypoints.g2};
  for (const NormalizedPoint& point : points) {
    if (!finiteNormalized(point)) {
      return false;
    }
  }
  const double values[] = {
      result.keypoints.confidence,
      result.measurements.left.m1,  result.measurements.left.m2,  result.measurements.left.m3,
      result.measurements.left.m4,  result.measurements.left.m5,  result.measurements.right.m1,
      result.measurements.right.m2, result.measurements.right.m3, result.measurements.right.m4,
      result.measurements.right.m5, result.measurements.confidence, result.asymmetry.features.m1,
      result.asymmetry.features.m2, result.asymmetry.features.m3, result.asymmetry.features.m4,
      result.asymmetry.features.m5, result.asymmetry.value,       result.quality.overallConfidence,
      result.quality.leafConfidence, result.quality.centerVeinConfidence,
      result.quality.secondaryVeinConfidence, result.quality.keypointConfidence,
      result.quality.measurementConfidence};
  for (const double value : values) {
    if (!finiteNumber(value)) {
      return false;
    }
  }
  return true;
}

bool jsonFinite(const Json& json) {
  if (json.is_number_float()) {
    return finiteNumber(json.get<double>());
  }
  if (json.is_object()) {
    for (auto it = json.begin(); it != json.end(); ++it) {
      if (!jsonFinite(it.value())) {
        return false;
      }
    }
  } else if (json.is_array()) {
    for (const Json& value : json) {
      if (!jsonFinite(value)) {
        return false;
      }
    }
  }
  return true;
}

Json pointJson(Point point) {
  Json json = Json::object();
  json["x"] = point.x;
  json["y"] = point.y;
  return json;
}

Json pathJson(const Path& path) {
  Json json = Json::array();
  for (const Point& point : path) {
    json.push_back(pointJson(point));
  }
  return json;
}

Json rectJson(Rect rect) {
  Json json = Json::object();
  json["x"] = rect.x;
  json["y"] = rect.y;
  json["width"] = rect.width;
  json["height"] = rect.height;
  return json;
}

Json versionsJson(const Versions& versions) {
  Json json = Json::object();
  json["resultSchemaVersion"] = versions.resultSchemaVersion;
  json["libraryVersion"] = versions.libraryVersion;
  json["algorithmVersion"] = versions.algorithmVersion;
  json["methodologyVersion"] = versions.methodologyVersion;
  json["modelVersion"] = versions.modelVersion ? Json(*versions.modelVersion) : Json(nullptr);
  return json;
}

Json veinJson(const Vein& vein) {
  Json json = Json::object();
  json["path"] = pathJson(vein.path);
  json["attachment"] = pointJson(vein.attachment);
  json["endpoint"] = pointJson(vein.endpoint);
  json["arcLength"] = vein.arcLength;
  json["confidence"] = vein.confidence;
  return json;
}

Json normalizedJson(const NormalizedPoint& point) {
  Json json = Json::object();
  json["image"] = pointJson(point.image);
  json["normalized"] = pointJson(point.normalized);
  return json;
}

Json sideJson(const SideMeasurements& side) {
  Json json = Json::object();
  json["m1"] = side.m1;
  json["m2"] = side.m2;
  json["m3"] = side.m3;
  json["m4"] = side.m4;
  json["m5"] = side.m5;
  return json;
}

Json analysisObject(const AnalysisResult& result) {
  Json geometry = Json::object();
  Json coordinate = Json::object();
  coordinate["origin"] = pointJson(result.coordinateSystem.origin);
  coordinate["xAxis"] = pointJson(result.coordinateSystem.xAxis);
  coordinate["yAxis"] = pointJson(result.coordinateSystem.yAxis);
  coordinate["scale"] = result.coordinateSystem.scale;

  Json leaf = Json::object();
  leaf["path"] = pathJson(result.leaf.path);
  leaf["area"] = result.leaf.area;
  leaf["boundingBox"] = rectJson(result.leaf.boundingBox);
  leaf["confidence"] = result.leaf.confidence;

  Json center = Json::object();
  center["path"] = pathJson(result.centerVein.path);
  center["base"] = pointJson(result.centerVein.base);
  center["apex"] = pointJson(result.centerVein.apex);
  center["confidence"] = result.centerVein.confidence;

  Json secondary = Json::object();
  secondary["leftFirst"] = veinJson(result.secondaryVeins.leftFirst);
  secondary["leftSecond"] = veinJson(result.secondaryVeins.leftSecond);
  secondary["rightFirst"] = veinJson(result.secondaryVeins.rightFirst);
  secondary["rightSecond"] = veinJson(result.secondaryVeins.rightSecond);
  secondary["confidence"] = result.secondaryVeins.confidence;

  Json keypoints = Json::object();
  keypoints["a1"] = normalizedJson(result.keypoints.a1);
  keypoints["a2"] = normalizedJson(result.keypoints.a2);
  keypoints["b1"] = normalizedJson(result.keypoints.b1);
  keypoints["b2"] = normalizedJson(result.keypoints.b2);
  keypoints["c1"] = normalizedJson(result.keypoints.c1);
  keypoints["c2"] = normalizedJson(result.keypoints.c2);
  keypoints["d1"] = normalizedJson(result.keypoints.d1);
  keypoints["d2"] = normalizedJson(result.keypoints.d2);
  keypoints["e"] = normalizedJson(result.keypoints.e);
  keypoints["f"] = normalizedJson(result.keypoints.f);
  keypoints["g1"] = normalizedJson(result.keypoints.g1);
  keypoints["g2"] = normalizedJson(result.keypoints.g2);
  keypoints["confidence"] = result.keypoints.confidence;

  geometry["coordinateSystem"] = std::move(coordinate);
  geometry["leaf"] = std::move(leaf);
  geometry["centerVein"] = std::move(center);
  geometry["secondaryVeins"] = std::move(secondary);
  geometry["keypoints"] = std::move(keypoints);

  Json measurements = Json::object();
  measurements["left"] = sideJson(result.measurements.left);
  measurements["right"] = sideJson(result.measurements.right);
  measurements["confidence"] = result.measurements.confidence;

  Json relative = Json::object();
  relative["m1"] = result.asymmetry.features.m1;
  relative["m2"] = result.asymmetry.features.m2;
  relative["m3"] = result.asymmetry.features.m3;
  relative["m4"] = result.asymmetry.features.m4;
  relative["m5"] = result.asymmetry.features.m5;

  Json asymmetry = Json::object();
  asymmetry["value"] = result.asymmetry.value;

  Json issues = Json::array();
  for (const QualityIssue issue : result.quality.issues) {
    issues.push_back(qualityIssueName(issue));
  }
  Json quality = Json::object();
  quality["acceptable"] = result.quality.acceptable;
  quality["overallConfidence"] = result.quality.overallConfidence;
  quality["leafConfidence"] = result.quality.leafConfidence;
  quality["centerVeinConfidence"] = result.quality.centerVeinConfidence;
  quality["secondaryVeinConfidence"] = result.quality.secondaryVeinConfidence;
  quality["keypointConfidence"] = result.quality.keypointConfidence;
  quality["measurementConfidence"] = result.quality.measurementConfidence;
  quality["sufficientContrast"] = result.quality.sufficientContrast;
  quality["sufficientLighting"] = result.quality.sufficientLighting;
  quality["issues"] = std::move(issues);

  Json json = Json::object();
  json["versions"] = versionsJson(result.versions);
  json["geometry"] = std::move(geometry);
  json["measurements"] = std::move(measurements);
  json["relativeAsymmetry"] = std::move(relative);
  json["asymmetry"] = std::move(asymmetry);
  json["score"] = result.score;
  json["quality"] = std::move(quality);
  return json;
}

Json errorObject(const Error& error) {
  Json json = Json::object();
  json["code"] = errorCodeName(error.code);
  json["stage"] = stageName(error.stage);
  json["message"] = error.message;
  return json;
}

std::string dumpFinite(const Json& json) {
  if (!jsonFinite(json)) {
    throw std::runtime_error("nonfinite");
  }
  return json.dump();
}

}  // namespace

Outcome<std::string> analysisResultToJson(const AnalysisResult& result) noexcept {
  try {
    if (!finiteResult(result)) {
      return serializationError("nonfinite");
    }
    return Outcome<std::string>::success(dumpFinite(analysisObject(result)));
  } catch (const std::bad_alloc&) {
    return Outcome<std::string>::failure(
        {ErrorCode::InternalError, Stage::Serialization, "allocation"});
  } catch (...) {
    return serializationError("nonfinite");
  }
}

Outcome<std::string> batchResultToJson(const BatchResult& result) noexcept {
  try {
    if (result.meanAsymmetry.has_value() && !finiteNumber(*result.meanAsymmetry)) {
      return serializationError("nonfinite");
    }
    Json items = Json::array();
    for (const BatchItem& item : result.items) {
      Json encoded = Json::object();
      if (item.outcome.hasValue()) {
        if (!finiteResult(*item.outcome.value())) {
          return serializationError("nonfinite");
        }
        encoded["success"] = true;
        encoded["result"] = analysisObject(*item.outcome.value());
      } else {
        encoded["success"] = false;
        encoded["error"] = errorObject(*item.outcome.error());
      }
      items.push_back(std::move(encoded));
    }
    Json json = Json::object();
    json["versions"] = versionsJson(result.versions);
    json["items"] = std::move(items);
    json["total"] = result.total;
    json["successful"] = result.successful;
    json["acceptable"] = result.acceptable;
    json["meanAsymmetry"] = result.meanAsymmetry ? Json(*result.meanAsymmetry) : Json(nullptr);
    json["valid"] = result.valid;
    json["score"] = result.score;
    return Outcome<std::string>::success(dumpFinite(json));
  } catch (const std::bad_alloc&) {
    return Outcome<std::string>::failure(
        {ErrorCode::InternalError, Stage::Serialization, "allocation"});
  } catch (...) {
    return serializationError("nonfinite");
  }
}

Outcome<std::string> errorToJson(const Error& error, const Versions& versions,
                                 std::optional<std::uint32_t> cAbiVersion) noexcept {
  try {
    Json json = Json::object();
    json["versions"] = versionsJson(versions);
    if (cAbiVersion.has_value()) {
      json["cAbiVersion"] = *cAbiVersion;
    }
    json["success"] = false;
    json["error"] = errorObject(error);
    return Outcome<std::string>::success(dumpFinite(json));
  } catch (const std::bad_alloc&) {
    return Outcome<std::string>::failure(
        {ErrorCode::InternalError, Stage::Serialization, "allocation"});
  } catch (...) {
    return serializationError("nonfinite");
  }
}

}  // namespace leaf
