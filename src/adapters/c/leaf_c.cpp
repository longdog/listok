#include <leaf/c/leaf.h>

#include <leaf/analyzer.h>
#include <leaf/json.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <utility>

struct leaf_analyzer_t {
  std::unique_ptr<leaf::Analyzer> analyzer;
};

namespace {

char* mallocString(const std::string& text) {
  char* buffer = static_cast<char*>(std::malloc(text.size() + 1));
  if (buffer == nullptr) {
    return nullptr;
  }
  std::memcpy(buffer, text.data(), text.size());
  buffer[text.size()] = '\0';
  return buffer;
}

bool storeError(char** out, const leaf::Error& error) {
  if (out == nullptr) {
    return true;
  }
  const auto json = leaf::errorToJson(error, leaf::defaultVersions(), 1u);
  if (!json.hasValue()) {
    return false;
  }
  *out = mallocString(*json.value());
  return *out != nullptr;
}

leaf_status_t configStatus(leaf::ErrorCode code) {
  switch (code) {
    case leaf::ErrorCode::InvalidConfigJson:
    case leaf::ErrorCode::UnknownConfigField:
    case leaf::ErrorCode::InvalidConfigValue:
    case leaf::ErrorCode::InvalidScoreRanges:
      return LEAF_STATUS_CONFIG_ERROR;
    case leaf::ErrorCode::SerializationFailed:
      return LEAF_STATUS_SERIALIZATION_ERROR;
    case leaf::ErrorCode::InternalError:
      return LEAF_STATUS_INTERNAL_ERROR;
    default:
      return LEAF_STATUS_INTERNAL_ERROR;
  }
}

leaf::PixelFormat toPixelFormat(leaf_pixel_format_t format, bool& ok) {
  ok = true;
  switch (format) {
    case LEAF_PIXEL_GRAY8:
      return leaf::PixelFormat::Gray8;
    case LEAF_PIXEL_RGB8:
      return leaf::PixelFormat::RGB8;
    case LEAF_PIXEL_RGBA8:
      return leaf::PixelFormat::RGBA8;
  }
  ok = false;
  return leaf::PixelFormat::Gray8;
}

}  // namespace

extern "C" {

uint32_t leaf_c_abi_version(void) { return LEAF_C_ABI_VERSION; }

leaf_status_t leaf_analyzer_create(uint32_t requested_abi_version, const char* config_json,
                                   leaf_analyzer_t** out_analyzer, char** out_error_json) {
  try {
    if (out_analyzer != nullptr) {
      *out_analyzer = nullptr;
    }
    if (out_error_json != nullptr) {
      *out_error_json = nullptr;
    }
    if (out_analyzer == nullptr) {
      return LEAF_STATUS_INVALID_ARGUMENT;
    }
    if (requested_abi_version != LEAF_C_ABI_VERSION) {
      const leaf::Error error{leaf::ErrorCode::InvalidConfigValue, leaf::Stage::Config,
                              "abi mismatch"};
      if (out_error_json != nullptr && !storeError(out_error_json, error)) {
        return LEAF_STATUS_OUT_OF_MEMORY;
      }
      return LEAF_STATUS_ABI_MISMATCH;
    }

    leaf::AnalyzerConfig config = leaf::defaultAnalyzerConfig();
    if (config_json != nullptr) {
      if (config_json[0] == '\0') {
        const leaf::Error error{leaf::ErrorCode::InvalidConfigJson, leaf::Stage::Config, "empty"};
        if (out_error_json != nullptr && !storeError(out_error_json, error)) {
          return LEAF_STATUS_OUT_OF_MEMORY;
        }
        return LEAF_STATUS_CONFIG_ERROR;
      }
      const auto parsed = leaf::parseAnalyzerConfigJson(config_json);
      if (!parsed.hasValue()) {
        if (out_error_json != nullptr && !storeError(out_error_json, *parsed.error())) {
          return LEAF_STATUS_OUT_OF_MEMORY;
        }
        return configStatus(parsed.error()->code);
      }
      config = *parsed.value();
    }

    auto created = leaf::Analyzer::create(std::move(config));
    if (!created.hasValue()) {
      if (out_error_json != nullptr && !storeError(out_error_json, *created.error())) {
        return LEAF_STATUS_OUT_OF_MEMORY;
      }
      return configStatus(created.error()->code);
    }

    auto* handle = new leaf_analyzer_t;
    handle->analyzer = std::move(*created.value());
    *out_analyzer = handle;
    return LEAF_STATUS_OK;
  } catch (const std::bad_alloc&) {
    if (out_analyzer != nullptr) {
      *out_analyzer = nullptr;
    }
    return LEAF_STATUS_OUT_OF_MEMORY;
  } catch (...) {
    if (out_analyzer != nullptr) {
      *out_analyzer = nullptr;
    }
    return LEAF_STATUS_INTERNAL_ERROR;
  }
}

leaf_status_t leaf_analyzer_analyze(leaf_analyzer_t* analyzer, const uint8_t* data, int32_t width,
                                    int32_t height, int32_t stride, leaf_pixel_format_t pixel_format,
                                    char** out_result_json) {
  try {
    if (out_result_json != nullptr) {
      *out_result_json = nullptr;
    }
    if (out_result_json == nullptr || analyzer == nullptr || analyzer->analyzer == nullptr) {
      return LEAF_STATUS_INVALID_ARGUMENT;
    }

    bool formatOk = false;
    const leaf::PixelFormat format = toPixelFormat(pixel_format, formatOk);
    if (!formatOk || data == nullptr || width <= 0 || height <= 0) {
      return LEAF_STATUS_INVALID_ARGUMENT;
    }

    leaf::ImageView view;
    view.data = data;
    view.width = width;
    view.height = height;
    view.stride = stride;
    view.format = format;
    const auto validated = leaf::validateImage(view);
    if (!validated.hasValue()) {
      return LEAF_STATUS_INVALID_ARGUMENT;
    }

    const auto result = analyzer->analyzer->analyze(*validated.value());
    if (!result.hasValue()) {
      if (!storeError(out_result_json, *result.error())) {
        return LEAF_STATUS_OUT_OF_MEMORY;
      }
      return LEAF_STATUS_ANALYSIS_ERROR;
    }

    const auto json = leaf::analysisResultToJson(*result.value());
    if (!json.hasValue()) {
      if (json.error()->code == leaf::ErrorCode::SerializationFailed) {
        return LEAF_STATUS_SERIALIZATION_ERROR;
      }
      if (!storeError(out_result_json, *json.error())) {
        return LEAF_STATUS_OUT_OF_MEMORY;
      }
      return LEAF_STATUS_SERIALIZATION_ERROR;
    }
    *out_result_json = mallocString(*json.value());
    if (*out_result_json == nullptr) {
      return LEAF_STATUS_OUT_OF_MEMORY;
    }
    return LEAF_STATUS_OK;
  } catch (const std::bad_alloc&) {
    return LEAF_STATUS_OUT_OF_MEMORY;
  } catch (...) {
    return LEAF_STATUS_INTERNAL_ERROR;
  }
}

void leaf_string_free(char* value) { std::free(value); }

void leaf_analyzer_destroy(leaf_analyzer_t* analyzer) { delete analyzer; }

}  // extern "C"
