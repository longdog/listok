#include <leaf/json.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace leaf {
namespace {

using Json = nlohmann::ordered_json;

Outcome<AnalyzerConfig> configError(ErrorCode code, std::string message) noexcept {
  return Outcome<AnalyzerConfig>::failure({code, Stage::Config, std::move(message)});
}

struct DomSax : nlohmann::json_sax<Json> {
  struct Frame {
    Json value = Json::object();
    std::string key;
    std::vector<std::string> keys;
    bool isArray = false;
  };

  std::vector<Frame> stack;
  Json root = nullptr;
  bool hasRoot = false;
  bool duplicate = false;

  bool pushValue(Json value) {
    if (stack.empty()) {
      root = std::move(value);
      hasRoot = true;
      return true;
    }
    Frame& frame = stack.back();
    if (frame.isArray) {
      frame.value.push_back(std::move(value));
    } else {
      frame.value[frame.key] = std::move(value);
    }
    return true;
  }

  bool null() override { return pushValue(nullptr); }
  bool boolean(bool value) override { return pushValue(value); }
  bool number_integer(number_integer_t value) override { return pushValue(value); }
  bool number_unsigned(number_unsigned_t value) override { return pushValue(value); }
  bool number_float(number_float_t value, const string_t&) override { return pushValue(value); }
  bool string(string_t& value) override { return pushValue(std::move(value)); }
  bool binary(binary_t&) override { return false; }

  bool start_object(std::size_t) override {
    Frame frame;
    frame.value = Json::object();
    stack.push_back(std::move(frame));
    return true;
  }

  bool key(string_t& value) override {
    Frame& frame = stack.back();
    if (std::find(frame.keys.begin(), frame.keys.end(), value) != frame.keys.end()) {
      duplicate = true;
      return false;
    }
    frame.keys.push_back(value);
    frame.key = std::move(value);
    return true;
  }

  bool end_object() override {
    Frame frame = std::move(stack.back());
    stack.pop_back();
    return pushValue(std::move(frame.value));
  }

  bool start_array(std::size_t) override {
    Frame frame;
    frame.value = Json::array();
    frame.isArray = true;
    stack.push_back(std::move(frame));
    return true;
  }

  bool end_array() override {
    Frame frame = std::move(stack.back());
    stack.pop_back();
    return pushValue(std::move(frame.value));
  }

  bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override {
    return false;
  }
};

bool isAllowed(const std::string& key, std::initializer_list<const char*> allowed) {
  for (const char* name : allowed) {
    if (key == name) {
      return true;
    }
  }
  return false;
}

bool readInt(const Json& value, int& out) {
  if (value.is_number_unsigned()) {
    const auto number = value.get<std::uint64_t>();
    if (number > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
      return false;
    }
    out = static_cast<int>(number);
    return true;
  }
  if (value.is_number_integer()) {
    const auto number = value.get<std::int64_t>();
    if (number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max()) {
      return false;
    }
    out = static_cast<int>(number);
    return true;
  }
  return false;
}

bool readDouble(const Json& value, double& out) {
  if (!value.is_number()) {
    return false;
  }
  const double number = value.get<double>();
  if (!std::isfinite(number)) {
    return false;
  }
  out = number;
  return true;
}

bool readBool(const Json& value, bool& out) {
  if (!value.is_boolean()) {
    return false;
  }
  out = value.get<bool>();
  return true;
}

bool rejectUnknown(const Json& object, std::initializer_list<const char*> allowed, Error& error) {
  for (auto it = object.begin(); it != object.end(); ++it) {
    if (!isAllowed(it.key(), allowed)) {
      error = {ErrorCode::UnknownConfigField, Stage::Config, it.key()};
      return false;
    }
  }
  return true;
}

bool requireObject(const Json& value, Error& error, const char* name) {
  if (!value.is_object()) {
    error = {ErrorCode::InvalidConfigValue, Stage::Config, name};
    return false;
  }
  return true;
}

bool applyPreprocess(const Json& object, PreprocessConfig& config, Error& error) {
  if (!requireObject(object, error, "preprocess") ||
      !rejectUnknown(object, {"targetMaxDimension", "normalizeIllumination", "blurKernel",
                              "adaptiveThreshold", "adaptiveThresholdBlockSize", "adaptiveThresholdC",
                              "minProcessLuminanceStdDev", "minAcceptableLuminanceStdDev",
                              "processMeanLuminanceMin", "processMeanLuminanceMax",
                              "acceptableMeanLuminanceMin", "acceptableMeanLuminanceMax"},
                     error)) {
    return false;
  }
  for (auto it = object.begin(); it != object.end(); ++it) {
    const std::string& key = it.key();
    bool ok = true;
    if (key == "targetMaxDimension") {
      ok = readInt(it.value(), config.targetMaxDimension);
    } else if (key == "normalizeIllumination") {
      ok = readBool(it.value(), config.normalizeIllumination);
    } else if (key == "blurKernel") {
      ok = readInt(it.value(), config.blurKernel);
    } else if (key == "adaptiveThreshold") {
      ok = readBool(it.value(), config.adaptiveThreshold);
    } else if (key == "adaptiveThresholdBlockSize") {
      ok = readInt(it.value(), config.adaptiveThresholdBlockSize);
    } else if (key == "adaptiveThresholdC") {
      ok = readDouble(it.value(), config.adaptiveThresholdC);
    } else if (key == "minProcessLuminanceStdDev") {
      ok = readDouble(it.value(), config.minProcessLuminanceStdDev);
    } else if (key == "minAcceptableLuminanceStdDev") {
      ok = readDouble(it.value(), config.minAcceptableLuminanceStdDev);
    } else if (key == "processMeanLuminanceMin") {
      ok = readDouble(it.value(), config.processMeanLuminanceMin);
    } else if (key == "processMeanLuminanceMax") {
      ok = readDouble(it.value(), config.processMeanLuminanceMax);
    } else if (key == "acceptableMeanLuminanceMin") {
      ok = readDouble(it.value(), config.acceptableMeanLuminanceMin);
    } else if (key == "acceptableMeanLuminanceMax") {
      ok = readDouble(it.value(), config.acceptableMeanLuminanceMax);
    }
    if (!ok) {
      error = {ErrorCode::InvalidConfigValue, Stage::Config, key};
      return false;
    }
  }
  return true;
}

bool applyDetection(const Json& object, DetectionConfig& config, Error& error) {
  if (!requireObject(object, error, "detection") ||
      !rejectUnknown(object,
                     {"minLeafAreaRatio", "maxLeafAreaRatio", "frameMarginPx", "minContourSolidity",
                      "maxContourCandidates", "minCenterVeinLengthRatio", "maxCenterVeinGapPx",
                      "veinThresholdBlockSize", "veinThresholdC", "minSkeletonBranchLengthNorm",
                      "minSecondaryVeinLengthNorm", "minSecondaryAngleDeg", "maxSecondaryAngleDeg",
                      "minAttachmentSeparationNorm", "endpointContourSnapPx", "branchMergeRadiusPx",
                      "ambiguityScoreDelta", "minimumStageConfidence"},
                     error)) {
    return false;
  }
  for (auto it = object.begin(); it != object.end(); ++it) {
    const std::string& key = it.key();
    bool ok = true;
    if (key == "minLeafAreaRatio") {
      ok = readDouble(it.value(), config.minLeafAreaRatio);
    } else if (key == "maxLeafAreaRatio") {
      ok = readDouble(it.value(), config.maxLeafAreaRatio);
    } else if (key == "frameMarginPx") {
      ok = readInt(it.value(), config.frameMarginPx);
    } else if (key == "minContourSolidity") {
      ok = readDouble(it.value(), config.minContourSolidity);
    } else if (key == "maxContourCandidates") {
      ok = readInt(it.value(), config.maxContourCandidates);
    } else if (key == "minCenterVeinLengthRatio") {
      ok = readDouble(it.value(), config.minCenterVeinLengthRatio);
    } else if (key == "maxCenterVeinGapPx") {
      ok = readInt(it.value(), config.maxCenterVeinGapPx);
    } else if (key == "veinThresholdBlockSize") {
      ok = readInt(it.value(), config.veinThresholdBlockSize);
    } else if (key == "veinThresholdC") {
      ok = readDouble(it.value(), config.veinThresholdC);
    } else if (key == "minSkeletonBranchLengthNorm") {
      ok = readDouble(it.value(), config.minSkeletonBranchLengthNorm);
    } else if (key == "minSecondaryVeinLengthNorm") {
      ok = readDouble(it.value(), config.minSecondaryVeinLengthNorm);
    } else if (key == "minSecondaryAngleDeg") {
      ok = readDouble(it.value(), config.minSecondaryAngleDeg);
    } else if (key == "maxSecondaryAngleDeg") {
      ok = readDouble(it.value(), config.maxSecondaryAngleDeg);
    } else if (key == "minAttachmentSeparationNorm") {
      ok = readDouble(it.value(), config.minAttachmentSeparationNorm);
    } else if (key == "endpointContourSnapPx") {
      ok = readDouble(it.value(), config.endpointContourSnapPx);
    } else if (key == "branchMergeRadiusPx") {
      ok = readDouble(it.value(), config.branchMergeRadiusPx);
    } else if (key == "ambiguityScoreDelta") {
      ok = readDouble(it.value(), config.ambiguityScoreDelta);
    } else if (key == "minimumStageConfidence") {
      ok = readDouble(it.value(), config.minimumStageConfidence);
    }
    if (!ok) {
      error = {ErrorCode::InvalidConfigValue, Stage::Config, key};
      return false;
    }
  }
  return true;
}

bool applyMeasurement(const Json& object, MeasurementConfig& config, Error& error) {
  if (!requireObject(object, error, "measurement") ||
      !rejectUnknown(object, {"minDenominator"}, error)) {
    return false;
  }
  if (object.contains("minDenominator") && !readDouble(object["minDenominator"], config.minDenominator)) {
    error = {ErrorCode::InvalidConfigValue, Stage::Config, "minDenominator"};
    return false;
  }
  return true;
}

bool applyQuality(const Json& object, QualityConfig& config, Error& error) {
  if (!requireObject(object, error, "quality") ||
      !rejectUnknown(object,
                     {"minimumLeafConfidence", "minimumCenterVeinConfidence",
                      "minimumSecondaryVeinConfidence", "minimumKeypointConfidence",
                      "minimumMeasurementConfidence"},
                     error)) {
    return false;
  }
  for (auto it = object.begin(); it != object.end(); ++it) {
    double* field = nullptr;
    if (it.key() == "minimumLeafConfidence") {
      field = &config.minimumLeafConfidence;
    } else if (it.key() == "minimumCenterVeinConfidence") {
      field = &config.minimumCenterVeinConfidence;
    } else if (it.key() == "minimumSecondaryVeinConfidence") {
      field = &config.minimumSecondaryVeinConfidence;
    } else if (it.key() == "minimumKeypointConfidence") {
      field = &config.minimumKeypointConfidence;
    } else if (it.key() == "minimumMeasurementConfidence") {
      field = &config.minimumMeasurementConfidence;
    }
    if (field == nullptr || !readDouble(it.value(), *field)) {
      error = {ErrorCode::InvalidConfigValue, Stage::Config, it.key()};
      return false;
    }
  }
  return true;
}

bool applyScoreRanges(const Json& value, std::array<ScoreRange, 5>& ranges, Error& error) {
  if (!value.is_array()) {
    error = {ErrorCode::InvalidConfigValue, Stage::Config, "scoreRanges"};
    return false;
  }
  for (const Json& item : value) {
    if (!item.is_object()) {
      error = {ErrorCode::InvalidConfigValue, Stage::Config, "scoreRanges"};
      return false;
    }
    if (!rejectUnknown(item, {"min", "max", "score"}, error)) {
      return false;
    }
  }
  if (value.size() != ranges.size()) {
    error = {ErrorCode::InvalidConfigValue, Stage::Config, "scoreRanges"};
    return false;
  }
  for (std::size_t index = 0; index < value.size(); ++index) {
    const Json& item = value[index];
    ScoreRange& range = ranges[index];
    if (item.contains("min") && !readDouble(item["min"], range.min)) {
      error = {ErrorCode::InvalidConfigValue, Stage::Config, "min"};
      return false;
    }
    if (item.contains("max")) {
      if (item["max"].is_null()) {
        range.max.reset();
      } else {
        double max = 0;
        if (!readDouble(item["max"], max)) {
          error = {ErrorCode::InvalidConfigValue, Stage::Config, "max"};
          return false;
        }
        range.max = max;
      }
    }
    if (item.contains("score") && !readInt(item["score"], range.score)) {
      error = {ErrorCode::InvalidConfigValue, Stage::Config, "score"};
      return false;
    }
  }
  return true;
}

}  // namespace

Outcome<AnalyzerConfig> parseAnalyzerConfigJson(std::string_view utf8) noexcept {
  try {
    if (utf8.empty()) {
      return configError(ErrorCode::InvalidConfigJson, "empty");
    }
    DomSax sax;
    const bool parsed = Json::sax_parse(std::string(utf8), &sax);
    if (sax.duplicate) {
      return configError(ErrorCode::InvalidConfigJson, "duplicate key");
    }
    if (!parsed || !sax.hasRoot || !sax.root.is_object()) {
      return configError(ErrorCode::InvalidConfigJson, "malformed");
    }

    Error error;
    if (!rejectUnknown(sax.root, {"preprocess", "detection", "measurement", "quality", "scoreRanges"},
                       error)) {
      return Outcome<AnalyzerConfig>::failure(error);
    }

    AnalyzerConfig config = defaultAnalyzerConfig();
    if (sax.root.contains("preprocess") && !applyPreprocess(sax.root["preprocess"], config.preprocess, error)) {
      return Outcome<AnalyzerConfig>::failure(error);
    }
    if (sax.root.contains("detection") && !applyDetection(sax.root["detection"], config.detection, error)) {
      return Outcome<AnalyzerConfig>::failure(error);
    }
    if (sax.root.contains("measurement") &&
        !applyMeasurement(sax.root["measurement"], config.measurement, error)) {
      return Outcome<AnalyzerConfig>::failure(error);
    }
    if (sax.root.contains("quality") && !applyQuality(sax.root["quality"], config.quality, error)) {
      return Outcome<AnalyzerConfig>::failure(error);
    }
    if (sax.root.contains("scoreRanges") && !applyScoreRanges(sax.root["scoreRanges"], config.scoreRanges, error)) {
      return Outcome<AnalyzerConfig>::failure(error);
    }
    return validateConfig(std::move(config));
  } catch (const std::bad_alloc&) {
    return Outcome<AnalyzerConfig>::failure(
        {ErrorCode::InternalError, Stage::Config, "allocation"});
  } catch (...) {
    return configError(ErrorCode::InvalidConfigJson, "malformed");
  }
}

}  // namespace leaf
