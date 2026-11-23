#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HOME_PROFILE_PHOTO_W 90
#define HOME_PROFILE_PHOTO_H 120
#define HOME_PROFILE_PHOTO_BYTES ((size_t)HOME_PROFILE_PHOTO_W * HOME_PROFILE_PHOTO_H * 2)

// Mount the storage partition in the background; safe to call more than once.
void home_photo_prepare(void);

bool home_photo_available(void);
uint32_t home_photo_version(void);
esp_err_t home_photo_read(uint8_t *dst, size_t capacity);
esp_err_t home_photo_read_range(size_t offset, uint8_t *dst, size_t length);

// Stream one exact-size little-endian RGB565 image into a temporary file,
// then replace the previous image after full-size validation.
esp_err_t home_photo_write_chunk(const uint8_t *data, size_t length,
                                 bool first, bool last, size_t total_bytes);
void home_photo_abort_upload(void);
esp_err_t home_photo_delete(void);

#ifdef __cplusplus
}
#endif
