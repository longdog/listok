#include <gtest/gtest.h>

#include <leaf/config.h>
#include <leaf/error.h>
#include <leaf/json.h>
#include <leaf/result.h>
#include <leaf/types.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string readExpectedText(const char* relative) {
  std::ifstream input(std::string(CMAKE_SOURCE_DIR) + "/" + relative, std::ios::binary);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

leaf::Outcome<leaf::AnalyzerConfig> parse(std::string_view json) {
  return leaf::parseAnalyzerConfigJson(json);
}

void expectCode(const leaf::Outcome<leaf::AnalyzerConfig>& outcome, leaf::ErrorCode code) {
  ASSERT_FALSE(outcome.hasValue());
  ASSERT_NE(outcome.error(), nullptr);
  EXPECT_EQ(outcome.error()->code, code);
}

#define EXPECT_CODE(expression, code) expectCode((expression), leaf::ErrorCode::code)

leaf::NormalizedPoint keypoint(double x, double y, double nx, double ny) {
  leaf::NormalizedPoint point;
  point.image = {x, y};
  point.normalized = {nx, ny};
  return point;
}

leaf::Vein veinAt(double y) {
  leaf::Vein vein;
  vein.path = {{0, y}, {1, y}};
  vein.attachment = {0, y};
  vein.endpoint = {1, y};
  vein.arcLength = 1;
  vein.confidence = 0.8;
  return vein;
}

leaf::AnalysisResult canonicalResult() {
  leaf::AnalysisResult result;
  result.versions = leaf::defaultVersions();
  result.coordinateSystem = {{0, 0}, {1, 0}, {0, 1}, 10};
  result.leaf.path = {{0, 0}, {1, 0}};
  result.leaf.area = 1;
  result.leaf.boundingBox = {0, 0, 1, 2};
  result.leaf.confidence = 1;
  result.centerVein.path = {{0, 0}, {0, 10}};
  result.centerVein.base = {0, 0};
  result.centerVein.apex = {0, 10};
  result.centerVein.confidence = 0.9;
  result.secondaryVeins.leftFirst = veinAt(2);
  result.secondaryVeins.leftSecond = veinAt(4);
  result.secondaryVeins.rightFirst = veinAt(2);
  result.secondaryVeins.rightSecond = veinAt(4.5);
  result.secondaryVeins.confidence = 0.7;
  result.keypoints.a1 = keypoint(0, 2, 0, 0.2);
  result.keypoints.a2 = keypoint(0, 2, 0, 0.2);
  result.keypoints.b1 = keypoint(0, 4, 0, 0.4);
  result.keypoints.b2 = keypoint(0, 4.5, 0, 0.45);
  result.keypoints.c1 = keypoint(1, 2, 0.1, 0.2);
  result.keypoints.c2 = keypoint(1, 2, 0.1, 0.2);
  result.keypoints.d1 = keypoint(1, 4, 0.1, 0.4);
  result.keypoints.d2 = keypoint(1, 4.5, 0.1, 0.45);
  result.keypoints.e = keypoint(0, 10, 0, 1);
  result.keypoints.f = keypoint(0, 5, 0, 0.5);
  result.keypoints.g1 = keypoint(-4, 5, -0.4, 0.5);
  result.keypoints.g2 = keypoint(5, 5, 0.5, 0.5);
  result.keypoints.confidence = 0.6;
  result.measurements.left = {0.4, 0.3, 0.2, 0.25, 45};
  result.measurements.right = {0.5, 0.4, 0.25, 0.3, 60};
  result.measurements.confidence = 0.55;
  result.asymmetry.features = {0.1, 0, -0.2, 0, 0.05};
  result.asymmetry.value = -0.01;
  result.score = 1;
  result.quality.acceptable = true;
  result.quality.overallConfidence = 0.55;
  result.quality.leafConfidence = 1;
  result.quality.centerVeinConfidence = 0.9;
  result.quality.secondaryVeinConfidence = 0.7;
  result.quality.keypointConfidence = 0.6;
  result.quality.measurementConfidence = 0.55;
  result.quality.sufficientContrast = true;
  result.quality.sufficientLighting = true;
  return result;
}

std::vector<std::string> objectKeys(const nlohmann::ordered_json& object) {
  std::vector<std::string> keys;
  for (auto it = object.begin(); it != object.end(); ++it) {
    keys.push_back(it.key());
  }
  return keys;
}

}  // namespace

TEST(json, RejectsDuplicateUnknownCoercionAndTrailingData) {
  EXPECT_CODE(parse(R"({"preprocess":{"blurKernel":5,"blurKernel":7}})"), InvalidConfigJson);
  EXPECT_CODE(parse(R"({"detection":{"enableMlFallback":false}})"), UnknownConfigField);
  EXPECT_CODE(parse(R"({"preprocess":{"targetMaxDimension":"2048"}})"), InvalidConfigValue);
  EXPECT_CODE(parse("{} trailing"), InvalidConfigJson);

  const auto serialized = leaf::analysisResultToJson(canonicalResult());
  ASSERT_TRUE(serialized.hasValue());
  EXPECT_EQ(*serialized.value(), readExpectedText("tests/expected/1.0/leaf_v1.json"));
  EXPECT_NE(serialized.value()->find("\"modelVersion\":null"), std::string::npos);

  const auto parsed = nlohmann::ordered_json::parse(*serialized.value());
  EXPECT_EQ(objectKeys(parsed),
            (std::vector<std::string>{"versions", "geometry", "measurements", "relativeAsymmetry",
                                      "asymmetry", "score", "quality"}));
  EXPECT_EQ(parsed["measurements"]["left"]["m1"], 0.4);
  EXPECT_EQ(parsed["measurements"]["right"]["m1"], 0.5);
  EXPECT_EQ(parsed["relativeAsymmetry"]["m3"], -0.2);
  EXPECT_EQ(parsed["asymmetry"]["value"], -0.01);
  EXPECT_EQ(parsed["score"], 1);
  EXPECT_EQ(parsed["quality"]["acceptable"], true);
  EXPECT_TRUE(parsed["versions"]["modelVersion"].is_null());
}

TEST(json, EmptyMalformedRootAndUtf8AreInvalidJson) {
  EXPECT_CODE(parse(""), InvalidConfigJson);
  EXPECT_CODE(parse("   "), InvalidConfigJson);
  EXPECT_CODE(parse("[]"), InvalidConfigJson);
  EXPECT_CODE(parse("null"), InvalidConfigJson);
  EXPECT_CODE(parse("{"), InvalidConfigJson);
  EXPECT_CODE(parse(std::string("\xff", 1)), InvalidConfigJson);
  EXPECT_CODE(parse(std::string("{\"a\":\xff}", 6)), InvalidConfigJson);

  const auto padded = parse(" \n{}\t");
  ASSERT_TRUE(padded.hasValue());
  EXPECT_EQ(padded.value()->preprocess.blurKernel, 2048 == 2048 ? 5 : 0);
}

TEST(json, OverlaysPresentFieldsOnDefaults) {
  const auto config = parse(R"({"preprocess":{"blurKernel":7},"detection":{"frameMarginPx":4}})");
  ASSERT_TRUE(config.hasValue());
  const auto defaults = leaf::defaultAnalyzerConfig();
  EXPECT_EQ(config.value()->preprocess.blurKernel, 7);
  EXPECT_EQ(config.value()->preprocess.targetMaxDimension, defaults.preprocess.targetMaxDimension);
  EXPECT_TRUE(config.value()->preprocess.normalizeIllumination);
  EXPECT_EQ(config.value()->detection.frameMarginPx, 4);
  EXPECT_DOUBLE_EQ(config.value()->detection.minLeafAreaRatio, defaults.detection.minLeafAreaRatio);
  EXPECT_DOUBLE_EQ(config.value()->measurement.minDenominator, defaults.measurement.minDenominator);
  EXPECT_DOUBLE_EQ(config.value()->quality.minimumLeafConfidence,
                   defaults.quality.minimumLeafConfidence);
  EXPECT_EQ(config.value()->scoreRanges[0].score, 1);
  EXPECT_FALSE(config.value()->scoreRanges[4].max.has_value());
}

TEST(json, RejectsUnknownFieldsAtEveryLevel) {
  EXPECT_CODE(parse(R"({"extra":1})"), UnknownConfigField);
  EXPECT_CODE(parse(R"({"preprocess":{"extra":1}})"), UnknownConfigField);
  EXPECT_CODE(parse(R"({"detection":{"extra":false}})"), UnknownConfigField);
  EXPECT_CODE(parse(R"({"measurement":{"extra":1}})"), UnknownConfigField);
  EXPECT_CODE(parse(R"({"quality":{"extra":1}})"), UnknownConfigField);
  EXPECT_CODE(parse(R"({"scoreRanges":[{"extra":1}]})"), UnknownConfigField);
  EXPECT_CODE(parse(R"({"preprocess":{},"preprocess":{}})"), InvalidConfigJson);
}

TEST(json, RejectsTypeCoercionNullAndFractionalIntegers) {
  EXPECT_CODE(parse(R"({"preprocess":{"normalizeIllumination":1}})"), InvalidConfigValue);
  EXPECT_CODE(parse(R"({"preprocess":{"adaptiveThreshold":"false"}})"), InvalidConfigValue);
  EXPECT_CODE(parse(R"({"preprocess":{"blurKernel":null}})"), InvalidConfigValue);
  EXPECT_CODE(parse(R"({"preprocess":{"blurKernel":5.0}})"), InvalidConfigValue);
  EXPECT_CODE(parse(R"({"preprocess":{"blurKernel":5.5}})"), InvalidConfigValue);
  EXPECT_CODE(parse(R"({"detection":{"veinThresholdC":"5"}})"), InvalidConfigValue);
  EXPECT_CODE(parse(R"({"measurement":{"minDenominator":null}})"), InvalidConfigValue);
  EXPECT_CODE(parse(R"({"scoreRanges":[{"min":"0","max":0.04,"score":1}]})"), InvalidConfigValue);
}

TEST(json, AcceptsFieldBoundariesAndRejectsOutOfRange) {
  struct Case {
    const char* json;
    bool valid;
  };
  const Case cases[] = {
      {R"({"preprocess":{"targetMaxDimension":256}})", true},
      {R"({"preprocess":{"targetMaxDimension":8192}})", true},
      {R"({"preprocess":{"targetMaxDimension":255}})", false},
      {R"({"preprocess":{"targetMaxDimension":8193}})", false},
      {R"({"preprocess":{"blurKernel":1}})", true},
      {R"({"preprocess":{"blurKernel":31}})", true},
      {R"({"preprocess":{"blurKernel":32}})", false},
      {R"({"preprocess":{"blurKernel":4}})", false},
      {R"({"preprocess":{"adaptiveThresholdBlockSize":3}})", true},
      {R"({"preprocess":{"adaptiveThresholdBlockSize":255}})", true},
      {R"({"preprocess":{"adaptiveThresholdBlockSize":4}})", false},
      {R"({"preprocess":{"adaptiveThresholdC":-64}})", true},
      {R"({"preprocess":{"adaptiveThresholdC":64}})", true},
      {R"({"preprocess":{"adaptiveThresholdC":64.1}})", false},
      {R"({"preprocess":{"minProcessLuminanceStdDev":8}})", true},
      {R"({"preprocess":{"minProcessLuminanceStdDev":0}})", false},
      {R"({"preprocess":{"minAcceptableLuminanceStdDev":12}})", true},
      {R"({"preprocess":{"minAcceptableLuminanceStdDev":7}})", false},
      {R"({"preprocess":{"processMeanLuminanceMin":10}})", true},
      {R"({"preprocess":{"processMeanLuminanceMin":21}})", false},
      {R"({"preprocess":{"processMeanLuminanceMax":245}})", true},
      {R"({"preprocess":{"processMeanLuminanceMax":230}})", false},
      {R"({"preprocess":{"acceptableMeanLuminanceMin":20}})", true},
      {R"({"preprocess":{"acceptableMeanLuminanceMin":9}})", false},
      {R"({"preprocess":{"acceptableMeanLuminanceMax":235}})", true},
      {R"({"preprocess":{"acceptableMeanLuminanceMax":246}})", false},
      {R"({"detection":{"minLeafAreaRatio":0.05}})", true},
      {R"({"detection":{"minLeafAreaRatio":0}})", false},
      {R"({"detection":{"maxLeafAreaRatio":1}})", true},
      {R"({"detection":{"maxLeafAreaRatio":0}})", false},
      {R"({"detection":{"frameMarginPx":0}})", true},
      {R"({"detection":{"frameMarginPx":64}})", true},
      {R"({"detection":{"frameMarginPx":65}})", false},
      {R"({"detection":{"minContourSolidity":0}})", true},
      {R"({"detection":{"minContourSolidity":1}})", true},
      {R"({"detection":{"minContourSolidity":1.01}})", false},
      {R"({"detection":{"maxContourCandidates":1}})", true},
      {R"({"detection":{"maxContourCandidates":64}})", true},
      {R"({"detection":{"maxContourCandidates":0}})", false},
      {R"({"detection":{"minCenterVeinLengthRatio":1}})", true},
      {R"({"detection":{"minCenterVeinLengthRatio":0}})", false},
      {R"({"detection":{"maxCenterVeinGapPx":0}})", true},
      {R"({"detection":{"maxCenterVeinGapPx":128}})", true},
      {R"({"detection":{"maxCenterVeinGapPx":129}})", false},
      {R"({"detection":{"veinThresholdBlockSize":3}})", true},
      {R"({"detection":{"veinThresholdBlockSize":255}})", true},
      {R"({"detection":{"veinThresholdBlockSize":2}})", false},
      {R"({"detection":{"veinThresholdC":-64}})", true},
      {R"({"detection":{"veinThresholdC":64}})", true},
      {R"({"detection":{"veinThresholdC":65}})", false},
      {R"({"detection":{"minSkeletonBranchLengthNorm":0.04}})", true},
      {R"({"detection":{"minSkeletonBranchLengthNorm":0}})", false},
      {R"({"detection":{"minSecondaryVeinLengthNorm":0.08}})", true},
      {R"({"detection":{"minSecondaryVeinLengthNorm":0.03}})", false},
      {R"({"detection":{"minSecondaryAngleDeg":0}})", true},
      {R"({"detection":{"minSecondaryAngleDeg":180}})", false},
      {R"({"detection":{"maxSecondaryAngleDeg":180}})", true},
      {R"({"detection":{"maxSecondaryAngleDeg":181}})", false},
      {R"({"detection":{"minAttachmentSeparationNorm":0.05}})", true},
      {R"({"detection":{"minAttachmentSeparationNorm":0}})", false},
      {R"({"detection":{"endpointContourSnapPx":0}})", true},
      {R"({"detection":{"endpointContourSnapPx":64}})", true},
      {R"({"detection":{"endpointContourSnapPx":64.1}})", false},
      {R"({"detection":{"branchMergeRadiusPx":0}})", true},
      {R"({"detection":{"branchMergeRadiusPx":32}})", true},
      {R"({"detection":{"branchMergeRadiusPx":32.1}})", false},
      {R"({"detection":{"ambiguityScoreDelta":0}})", true},
      {R"({"detection":{"ambiguityScoreDelta":1}})", true},
      {R"({"detection":{"ambiguityScoreDelta":1.1}})", false},
      {R"({"detection":{"minimumStageConfidence":0}})", true},
      {R"({"detection":{"minimumStageConfidence":1},"quality":{"minimumLeafConfidence":1,"minimumCenterVeinConfidence":1,"minimumSecondaryVeinConfidence":1,"minimumKeypointConfidence":1,"minimumMeasurementConfidence":1}})", true},
      {R"({"detection":{"minimumStageConfidence":-0.01}})", false},
      {R"({"measurement":{"minDenominator":1e-9}})", true},
      {R"({"measurement":{"minDenominator":0}})", false},
      {R"({"quality":{"minimumLeafConfidence":0.5}})", true},
      {R"({"quality":{"minimumLeafConfidence":1}})", true},
      {R"({"quality":{"minimumLeafConfidence":0.49}})", false},
      {R"({"quality":{"minimumCenterVeinConfidence":0.5}})", true},
      {R"({"quality":{"minimumCenterVeinConfidence":0.49}})", false},
      {R"({"quality":{"minimumSecondaryVeinConfidence":0.5}})", true},
      {R"({"quality":{"minimumSecondaryVeinConfidence":0.49}})", false},
      {R"({"quality":{"minimumKeypointConfidence":0.5}})", true},
      {R"({"quality":{"minimumKeypointConfidence":0.49}})", false},
      {R"({"quality":{"minimumMeasurementConfidence":0.5}})", true},
      {R"({"quality":{"minimumMeasurementConfidence":0.49}})", false},
  };
  for (const Case& item : cases) {
    const auto outcome = parse(item.json);
    EXPECT_EQ(outcome.hasValue(), item.valid) << item.json;
    if (!item.valid) {
      ASSERT_NE(outcome.error(), nullptr) << item.json;
      EXPECT_EQ(outcome.error()->code, leaf::ErrorCode::InvalidConfigValue) << item.json;
    }
  }
}

TEST(json, RejectsMalformedScoreRanges) {
  const auto outcome = parse(
      R"({"scoreRanges":[{"min":0.1,"max":0.2,"score":1},{"min":0.2,"max":0.3,"score":2},{"min":0.3,"max":0.4,"score":3},{"min":0.4,"max":0.5,"score":4},{"min":0.5,"max":null,"score":5}]})");
  ASSERT_FALSE(outcome.hasValue());
  EXPECT_EQ(outcome.error()->code, leaf::ErrorCode::InvalidScoreRanges);
  EXPECT_EQ(outcome.error()->stage, leaf::Stage::Score);
}

TEST(json, SerializesErrorEnvelopeEnumsAndRepeatsBytes) {
  const auto versions = leaf::defaultVersions();
  const auto error = leaf::errorToJson(
      {leaf::ErrorCode::LeafNotFound, leaf::Stage::LeafContour, "missing"}, versions, 1u);
  ASSERT_TRUE(error.hasValue());
  const auto parsed = nlohmann::ordered_json::parse(*error.value());
  EXPECT_EQ(objectKeys(parsed),
            (std::vector<std::string>{"versions", "cAbiVersion", "success", "error"}));
  EXPECT_EQ(parsed["cAbiVersion"], 1);
  EXPECT_EQ(parsed["success"], false);
  EXPECT_EQ(objectKeys(parsed["error"]), (std::vector<std::string>{"code", "stage", "message"}));
  EXPECT_EQ(parsed["error"]["code"], "LeafNotFound");
  EXPECT_EQ(parsed["error"]["stage"], "LeafContour");

  const auto withoutAbi = leaf::errorToJson(
      {leaf::ErrorCode::InvalidImage, leaf::Stage::Input, "bad"}, versions, std::nullopt);
  ASSERT_TRUE(withoutAbi.hasValue());
  EXPECT_EQ(withoutAbi.value()->find("cAbiVersion"), std::string::npos);

  const std::pair<leaf::ErrorCode, const char*> codes[] = {
      {leaf::ErrorCode::None, "None"},
      {leaf::ErrorCode::InvalidImage, "InvalidImage"},
      {leaf::ErrorCode::UnsupportedPixelFormat, "UnsupportedPixelFormat"},
      {leaf::ErrorCode::InvalidConfigJson, "InvalidConfigJson"},
      {leaf::ErrorCode::UnknownConfigField, "UnknownConfigField"},
      {leaf::ErrorCode::InvalidConfigValue, "InvalidConfigValue"},
      {leaf::ErrorCode::InsufficientContrast, "InsufficientContrast"},
      {leaf::ErrorCode::InsufficientLighting, "InsufficientLighting"},
      {leaf::ErrorCode::PreprocessFailed, "PreprocessFailed"},
      {leaf::ErrorCode::LeafNotFound, "LeafNotFound"},
      {leaf::ErrorCode::LeafTooSmall, "LeafTooSmall"},
      {leaf::ErrorCode::LeafOutsideFrame, "LeafOutsideFrame"},
      {leaf::ErrorCode::AmbiguousLeafContour, "AmbiguousLeafContour"},
      {leaf::ErrorCode::CentralVeinNotFound, "CentralVeinNotFound"},
      {leaf::ErrorCode::AmbiguousCentralVein, "AmbiguousCentralVein"},
      {leaf::ErrorCode::InvalidCoordinateSystem, "InvalidCoordinateSystem"},
      {leaf::ErrorCode::SkeletonizationFailed, "SkeletonizationFailed"},
      {leaf::ErrorCode::SecondaryVeinNotFound, "SecondaryVeinNotFound"},
      {leaf::ErrorCode::AmbiguousVeins, "AmbiguousVeins"},
      {leaf::ErrorCode::InvalidKeypoints, "InvalidKeypoints"},
      {leaf::ErrorCode::InvalidMeasurements, "InvalidMeasurements"},
      {leaf::ErrorCode::ZeroAsymmetryDenominator, "ZeroAsymmetryDenominator"},
      {leaf::ErrorCode::InvalidScoreRanges, "InvalidScoreRanges"},
      {leaf::ErrorCode::SerializationFailed, "SerializationFailed"},
      {leaf::ErrorCode::InternalError, "InternalError"},
  };
  for (const auto& [code, name] : codes) {
    const auto json = leaf::errorToJson({code, leaf::Stage::Internal, "m"}, versions);
    ASSERT_TRUE(json.hasValue()) << name;
    EXPECT_NE(json.value()->find(std::string("\"code\":\"") + name + "\""), std::string::npos) << name;
  }

  const std::pair<leaf::Stage, const char*> stages[] = {
      {leaf::Stage::Input, "Input"},
      {leaf::Stage::Config, "Config"},
      {leaf::Stage::Preprocess, "Preprocess"},
      {leaf::Stage::LeafContour, "LeafContour"},
      {leaf::Stage::CenterVein, "CenterVein"},
      {leaf::Stage::CoordinateSystem, "CoordinateSystem"},
      {leaf::Stage::Skeleton, "Skeleton"},
      {leaf::Stage::SecondaryVeins, "SecondaryVeins"},
      {leaf::Stage::Keypoints, "Keypoints"},
      {leaf::Stage::Measurements, "Measurements"},
      {leaf::Stage::Statistics, "Statistics"},
      {leaf::Stage::Score, "Score"},
      {leaf::Stage::Quality, "Quality"},
      {leaf::Stage::Serialization, "Serialization"},
      {leaf::Stage::Internal, "Internal"},
  };
  for (const auto& [stage, name] : stages) {
    const auto json = leaf::errorToJson({leaf::ErrorCode::InternalError, stage, "m"}, versions);
    ASSERT_TRUE(json.hasValue()) << name;
    EXPECT_NE(json.value()->find(std::string("\"stage\":\"") + name + "\""), std::string::npos) << name;
  }

  auto result = canonicalResult();
  result.quality.issues = {
      leaf::QualityIssue::LowLeafConfidence,      leaf::QualityIssue::LowCenterVeinConfidence,
      leaf::QualityIssue::LowSecondaryVeinConfidence, leaf::QualityIssue::LowKeypointConfidence,
      leaf::QualityIssue::LowMeasurementConfidence, leaf::QualityIssue::MarginalContrast,
      leaf::QualityIssue::MarginalLighting,
  };
  const auto withIssues = leaf::analysisResultToJson(result);
  ASSERT_TRUE(withIssues.hasValue());
  const auto issues = nlohmann::ordered_json::parse(*withIssues.value())["quality"]["issues"];
  EXPECT_EQ(issues,
            nlohmann::ordered_json::parse(
                R"(["LowLeafConfidence","LowCenterVeinConfidence","LowSecondaryVeinConfidence","LowKeypointConfidence","LowMeasurementConfidence","MarginalContrast","MarginalLighting"])"));

  const auto first = leaf::analysisResultToJson(canonicalResult());
  ASSERT_TRUE(first.hasValue());
  for (int i = 0; i < 100; ++i) {
    const auto again = leaf::analysisResultToJson(canonicalResult());
    ASSERT_TRUE(again.hasValue());
    ASSERT_EQ(*again.value(), *first.value()) << i;
  }
}

TEST(json, RejectsNonFiniteResults) {
  auto result = canonicalResult();
  result.measurements.left.m1 = std::numeric_limits<double>::quiet_NaN();
  const auto nanResult = leaf::analysisResultToJson(result);
  ASSERT_FALSE(nanResult.hasValue());
  EXPECT_EQ(nanResult.error()->code, leaf::ErrorCode::SerializationFailed);
  EXPECT_EQ(nanResult.error()->stage, leaf::Stage::Serialization);

  result = canonicalResult();
  result.asymmetry.value = std::numeric_limits<double>::infinity();
  const auto infResult = leaf::analysisResultToJson(result);
  ASSERT_FALSE(infResult.hasValue());
  EXPECT_EQ(infResult.error()->code, leaf::ErrorCode::SerializationFailed);
}

TEST(json, SerializesBatchWithNullMean) {
  leaf::BatchResult batch;
  batch.versions = leaf::defaultVersions();
  batch.total = 1;
  batch.successful = 0;
  batch.acceptable = 0;
  batch.valid = false;
  batch.score = 0;
  batch.items.push_back(leaf::BatchItem{leaf::Outcome<leaf::AnalysisResult>::failure(
      {leaf::ErrorCode::LeafNotFound, leaf::Stage::LeafContour, "missing"})});
  const auto json = leaf::batchResultToJson(batch);
  ASSERT_TRUE(json.hasValue());
  const auto parsed = nlohmann::ordered_json::parse(*json.value());
  EXPECT_EQ(objectKeys(parsed),
            (std::vector<std::string>{"versions", "items", "total", "successful", "acceptable",
                                      "meanAsymmetry", "valid", "score"}));
  EXPECT_TRUE(parsed["meanAsymmetry"].is_null());
  EXPECT_EQ(parsed["items"][0]["success"], false);
  EXPECT_EQ(parsed["items"][0]["error"]["code"], "LeafNotFound");
  EXPECT_EQ(parsed["score"], 0);
}
