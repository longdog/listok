#include <leaf/c/leaf.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* readFile(const char* path) {
  FILE* file = fopen(path, "rb");
  assert(file);
  assert(fseek(file, 0, SEEK_END) == 0);
  long size = ftell(file);
  assert(size >= 0);
  assert(fseek(file, 0, SEEK_SET) == 0);
  char* buffer = (char*)malloc((size_t)size + 1);
  assert(buffer);
  assert(fread(buffer, 1, (size_t)size, file) == (size_t)size);
  buffer[size] = '\0';
  fclose(file);
  return buffer;
}

static int loadPpm(const char* path, uint8_t** data, int32_t* width, int32_t* height) {
  FILE* file = fopen(path, "rb");
  if (!file) return 0;
  char magic[8];
  int maxv = 0;
  if (fscanf(file, "%7s %d %d %d", magic, width, height, &maxv) != 4 || strcmp(magic, "P6") != 0) {
    fclose(file);
    return 0;
  }
  if (fgetc(file) == EOF) {
    fclose(file);
    return 0;
  }
  size_t bytes = (size_t)(*width) * (size_t)(*height) * 3u;
  *data = (uint8_t*)malloc(bytes);
  if (!*data || fread(*data, 1, bytes, file) != bytes) {
    free(*data);
    fclose(file);
    return 0;
  }
  fclose(file);
  return 1;
}

int main(void) {
  assert(leaf_c_abi_version() == 1u);
  assert(LEAF_STATUS_OK == 0);
  assert(LEAF_STATUS_INVALID_ARGUMENT == 1);
  assert(LEAF_STATUS_CONFIG_ERROR == 2);
  assert(LEAF_STATUS_ANALYSIS_ERROR == 3);
  assert(LEAF_STATUS_SERIALIZATION_ERROR == 4);
  assert(LEAF_STATUS_OUT_OF_MEMORY == 5);
  assert(LEAF_STATUS_INTERNAL_ERROR == 6);
  assert(LEAF_STATUS_ABI_MISMATCH == 7);

  leaf_analyzer_t* analyzer = (leaf_analyzer_t*)0x1;
  char* error = (char*)0x1;
  assert(leaf_analyzer_create(99u, NULL, &analyzer, &error) == LEAF_STATUS_ABI_MISMATCH);
  assert(analyzer == NULL);
  assert(error != NULL);
  assert(strstr(error, "cAbiVersion") != NULL);
  leaf_string_free(error);

  assert(leaf_analyzer_create(1u, NULL, NULL, &error) == LEAF_STATUS_INVALID_ARGUMENT);
  assert(error == NULL);

  analyzer = (leaf_analyzer_t*)0x1;
  error = (char*)0x1;
  assert(leaf_analyzer_create(1u, "", &analyzer, &error) == LEAF_STATUS_CONFIG_ERROR);
  assert(analyzer == NULL);
  assert(error != NULL);
  leaf_string_free(error);

  assert(leaf_analyzer_create(1u, "{", &analyzer, &error) == LEAF_STATUS_CONFIG_ERROR);
  leaf_string_free(error);

  assert(leaf_analyzer_create(1u, NULL, &analyzer, &error) == LEAF_STATUS_OK);
  assert(analyzer != NULL);
  assert(error == NULL);

  char* out = (char*)0x1;
  assert(leaf_analyzer_analyze(analyzer, NULL, 1, 1, 1, LEAF_PIXEL_GRAY8, &out) ==
         LEAF_STATUS_INVALID_ARGUMENT);
  assert(out == NULL);
  assert(leaf_analyzer_analyze(NULL, (const uint8_t*)"x", 1, 1, 1, LEAF_PIXEL_GRAY8, &out) ==
         LEAF_STATUS_INVALID_ARGUMENT);
  assert(out == NULL);
  uint8_t pixel = 0;
  assert(leaf_analyzer_analyze(analyzer, &pixel, 0, 1, 1, LEAF_PIXEL_GRAY8, &out) ==
         LEAF_STATUS_INVALID_ARGUMENT);
  assert(leaf_analyzer_analyze(analyzer, &pixel, 1, -1, 1, LEAF_PIXEL_GRAY8, &out) ==
         LEAF_STATUS_INVALID_ARGUMENT);
  assert(leaf_analyzer_analyze(analyzer, &pixel, 1, 1, 0, LEAF_PIXEL_GRAY8, &out) ==
         LEAF_STATUS_INVALID_ARGUMENT);
  assert(leaf_analyzer_analyze(analyzer, &pixel, 1, 1, 1, (leaf_pixel_format_t)99, &out) ==
         LEAF_STATUS_INVALID_ARGUMENT);
  assert(leaf_analyzer_analyze(analyzer, &pixel, 1, 1, 1, LEAF_PIXEL_GRAY8, NULL) ==
         LEAF_STATUS_INVALID_ARGUMENT);

  uint8_t tiny[16] = {0};
  assert(leaf_analyzer_analyze(analyzer, tiny, 4, 4, 4, LEAF_PIXEL_GRAY8, &out) ==
         LEAF_STATUS_ANALYSIS_ERROR);
  assert(out != NULL);
  assert(strstr(out, "\"cAbiVersion\":1") != NULL);
  leaf_string_free(out);
  leaf_analyzer_destroy(analyzer);

  char* config = readFile(LEAF_CROSS_CONFIG);
  analyzer = NULL;
  error = (char*)0x1;
  assert(leaf_analyzer_create(1u, config, &analyzer, &error) == LEAF_STATUS_OK);
  assert(error == NULL);
  free(config);

  uint8_t* pixels = NULL;
  int32_t width = 0;
  int32_t height = 0;
  assert(loadPpm(LEAF_CROSS_PPM, &pixels, &width, &height));
  out = (char*)0x1;
  assert(leaf_analyzer_analyze(analyzer, pixels, width, height, width * 3, LEAF_PIXEL_RGB8, &out) ==
         LEAF_STATUS_OK);
  assert(out != NULL);
  assert(strstr(out, "\"acceptable\":false") != NULL);
  assert(strstr(out, "\"m1\"") != NULL);
  leaf_string_free(out);
  free(pixels);

  leaf_string_free(NULL);
  leaf_analyzer_destroy(NULL);
  leaf_analyzer_destroy(analyzer);
  return 0;
}
