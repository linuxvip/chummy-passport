#include "apps/home/home_photo.h"
#include "freertos/FreeRTOS.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void vTaskDelay(TickType_t ticks) {
    (void)ticks;
}

static void put_pattern(uint8_t *bytes, size_t length, uint8_t seed) {
    for (size_t i = 0; i < length; ++i) {
        bytes[i] = (uint8_t)(seed + i * 31u);
    }
}

static void upload(const uint8_t *bytes, size_t total) {
    const size_t chunk = 997;
    size_t offset = 0;
    bool first = true;
    while (offset < total) {
        size_t length = total - offset;
        if (length > chunk) length = chunk;
        assert(home_photo_write_chunk(bytes + offset, length, first,
                                      offset + length == total, total) == ESP_OK);
        first = false;
        offset += length;
    }
}

int main(void) {
    uint8_t original[HOME_PROFILE_PHOTO_BYTES];
    uint8_t replacement[HOME_PROFILE_PHOTO_BYTES];
    uint8_t legacy[HOME_PROFILE_PHOTO_LEGACY_BYTES];
    uint8_t readback[HOME_PROFILE_PHOTO_BYTES];
    put_pattern(original, sizeof(original), 7);
    put_pattern(replacement, sizeof(replacement), 199);
    put_pattern(legacy, sizeof(legacy), 77);
    mkdir(HOME_PHOTO_BASE_PATH, 0700);
    remove(HOME_PHOTO_BASE_PATH "/profile-photo.rgb565");
    remove(HOME_PHOTO_BASE_PATH "/profile-photo.tmp");
    remove(HOME_PHOTO_BASE_PATH "/profile-photo.scaled");
    remove(HOME_PHOTO_BASE_PATH "/profile-photo.backup");

    upload(original, sizeof(original));
    assert(home_photo_available());
    assert(home_photo_read(readback, sizeof(readback)) == ESP_OK);
    assert(memcmp(readback, original, sizeof(original)) == 0);
    assert(home_photo_read_range(1000, readback, 512) == ESP_OK);
    assert(memcmp(readback, original + 1000, 512) == 0);

    const uint32_t before_invalid = home_photo_version();
    assert(home_photo_write_chunk(original, 100, true, false,
                                  HOME_PROFILE_PHOTO_BYTES - 1) == ESP_ERR_INVALID_ARG);
    assert(home_photo_version() == before_invalid);
    assert(home_photo_read(readback, sizeof(readback)) == ESP_OK);
    assert(memcmp(readback, original, sizeof(original)) == 0);

    assert(home_photo_write_chunk(replacement, 100, true, false,
                                  HOME_PROFILE_PHOTO_BYTES) == ESP_OK);
    home_photo_abort_upload();
    assert(home_photo_read(readback, sizeof(readback)) == ESP_OK);
    assert(memcmp(readback, original, sizeof(original)) == 0);

    const uint32_t before_replace = home_photo_version();
    upload(replacement, sizeof(replacement));
    assert(home_photo_version() == before_replace + 1);
    assert(home_photo_read(readback, sizeof(readback)) == ESP_OK);
    assert(memcmp(readback, replacement, sizeof(replacement)) == 0);

    const uint32_t before_legacy = home_photo_version();
    upload(legacy, sizeof(legacy));
    assert(home_photo_version() == before_legacy + 1);
    assert(home_photo_available());
    assert(home_photo_read(readback, sizeof(readback)) == ESP_OK);
    assert(memcmp(readback, legacy, 2) == 0);

    assert(home_photo_delete() == ESP_OK);
    assert(!home_photo_available());
    assert(home_photo_read(readback, sizeof(readback)) == ESP_ERR_NOT_FOUND);
    remove(HOME_PHOTO_BASE_PATH "/profile-photo.rgb565");
    remove(HOME_PHOTO_BASE_PATH "/profile-photo.tmp");
    remove(HOME_PHOTO_BASE_PATH "/profile-photo.scaled");
    remove(HOME_PHOTO_BASE_PATH "/profile-photo.backup");
    rmdir(HOME_PHOTO_BASE_PATH);
    puts("Home profile photo: exact RGB565 size, chunked upload, abort and atomic replace PASS");
    return 0;
}
