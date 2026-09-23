#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LEAF_C_ABI_VERSION 1u

/*
 * ABI v1 ownership and threading:
 * - Returned strings are allocated with one malloc and must be released only
 *   with leaf_string_free. leaf_string_free(NULL) is a no-op.
 * - leaf_analyzer_destroy(NULL) is a no-op. Destroying the same non-null
 *   handle twice is undefined.
 * - Distinct analyzer handles may be used concurrently. The same handle must
 *   not be used concurrently, and destroy must not race with analyze.
 * - No function retains the pixel pointer after it returns.
 * - OpenCV types are not part of this header.
 */

typedef struct leaf_analyzer_t leaf_analyzer_t;

typedef enum leaf_pixel_format_t {
  LEAF_PIXEL_GRAY8 = 0,
  LEAF_PIXEL_RGB8 = 1,
  LEAF_PIXEL_RGBA8 = 2,
} leaf_pixel_format_t;

typedef enum leaf_status_t {
  LEAF_STATUS_OK = 0,
  LEAF_STATUS_INVALID_ARGUMENT = 1,
  LEAF_STATUS_CONFIG_ERROR = 2,
  LEAF_STATUS_ANALYSIS_ERROR = 3,
  LEAF_STATUS_SERIALIZATION_ERROR = 4,
  LEAF_STATUS_OUT_OF_MEMORY = 5,
  LEAF_STATUS_INTERNAL_ERROR = 6,
  LEAF_STATUS_ABI_MISMATCH = 7,
} leaf_status_t;

uint32_t leaf_c_abi_version(void);

leaf_status_t leaf_analyzer_create(uint32_t requested_abi_version,
                                   const char* config_json,
                                   leaf_analyzer_t** out_analyzer,
                                   char** out_error_json);

leaf_status_t leaf_analyzer_analyze(leaf_analyzer_t* analyzer,
                                    const uint8_t* data,
                                    int32_t width,
                                    int32_t height,
                                    int32_t stride,
                                    leaf_pixel_format_t pixel_format,
                                    char** out_result_json);

void leaf_string_free(char* value);
void leaf_analyzer_destroy(leaf_analyzer_t* analyzer);

#ifdef __cplusplus
}
#endif
