#include "apps/home/home_photo.h"

#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "home_photo";
#ifndef HOME_PHOTO_BASE_PATH
#define HOME_PHOTO_BASE_PATH "/storage"
#endif
static const char *const kPath = HOME_PHOTO_BASE_PATH "/profile-photo.rgb565";
static const char *const kTmpPath = HOME_PHOTO_BASE_PATH "/profile-photo.tmp";
static const char *const kBackupPath = HOME_PHOTO_BASE_PATH "/profile-photo.backup";

static StaticSemaphore_t s_mutex_storage;
static SemaphoreHandle_t s_mutex;
static bool s_mounted;
static FILE *s_upload;
static size_t s_written;
static uint32_t s_version;

static SemaphoreHandle_t lock(void) {
    if (!s_mutex) s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_storage);
    return s_mutex;
}

static void abort_upload_locked(void) {
    if (s_upload) {
        fclose(s_upload);
        s_upload = NULL;
    }
    remove(kTmpPath);
    s_written = 0;
}

static bool file_has_expected_size(const char *path) {
    struct stat info = {0};
    return stat(path, &info) == 0 && info.st_size == (off_t)HOME_PROFILE_PHOTO_BYTES;
}

static esp_err_t mount_locked(void) {
    if (s_mounted) return ESP_OK;
    esp_vfs_spiffs_conf_t config = {
        .base_path = HOME_PHOTO_BASE_PATH,
        .partition_label = "storage",
        .max_files = 3,
        .format_if_mount_failed = false,
    };
    esp_err_t err = esp_vfs_spiffs_register(&config);
    if (err != ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(50));
        config.format_if_mount_failed = true;
        err = esp_vfs_spiffs_register(&config);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS unavailable: %s", esp_err_to_name(err));
        return err;
    }

    s_mounted = true;
    // Recover a backup left by an interrupted replacement. A valid primary
    // always wins; otherwise the previous complete image is restored.
    if (!file_has_expected_size(kPath) && file_has_expected_size(kBackupPath)) {
        remove(kPath);
        rename(kBackupPath, kPath);
    }
    if (file_has_expected_size(kPath)) remove(kBackupPath);
    return err;
}

void home_photo_prepare(void) {
    SemaphoreHandle_t guard = lock();
    if (!guard) return;
    xSemaphoreTake(guard, portMAX_DELAY);
    mount_locked();
    xSemaphoreGive(guard);
}

bool home_photo_available(void) {
    SemaphoreHandle_t guard = lock();
    if (!guard) return false;
    xSemaphoreTake(guard, portMAX_DELAY);
    const bool available = mount_locked() == ESP_OK && file_has_expected_size(kPath);
    xSemaphoreGive(guard);
    return available;
}

uint32_t home_photo_version(void) {
    SemaphoreHandle_t guard = lock();
    if (!guard) return 0;
    xSemaphoreTake(guard, portMAX_DELAY);
    const uint32_t version = s_version;
    xSemaphoreGive(guard);
    return version;
}

esp_err_t home_photo_read(uint8_t *dst, size_t capacity) {
    if (!dst || capacity < HOME_PROFILE_PHOTO_BYTES) return ESP_ERR_INVALID_ARG;
    return home_photo_read_range(0, dst, HOME_PROFILE_PHOTO_BYTES);
}

esp_err_t home_photo_read_range(size_t offset, uint8_t *dst, size_t length) {
    if (!dst || !length || offset > HOME_PROFILE_PHOTO_BYTES ||
        length > HOME_PROFILE_PHOTO_BYTES - offset) return ESP_ERR_INVALID_ARG;
    SemaphoreHandle_t guard = lock();
    if (!guard) return ESP_ERR_NO_MEM;
    xSemaphoreTake(guard, portMAX_DELAY);
    esp_err_t err = mount_locked();
    if (err == ESP_OK) {
        FILE *file = fopen(kPath, "rb");
        if (!file) {
            err = ESP_ERR_NOT_FOUND;
        } else if (fseek(file, (long)offset, SEEK_SET) != 0) {
            err = ESP_FAIL;
            fclose(file);
        } else {
            const size_t got = fread(dst, 1, length, file);
            fclose(file);
            err = got == length ? ESP_OK : ESP_ERR_INVALID_SIZE;
        }
    }
    xSemaphoreGive(guard);
    return err;
}

void home_photo_abort_upload(void) {
    SemaphoreHandle_t guard = lock();
    if (!guard) return;
    xSemaphoreTake(guard, portMAX_DELAY);
    abort_upload_locked();
    xSemaphoreGive(guard);
}

esp_err_t home_photo_write_chunk(const uint8_t *data, size_t length,
                                 bool first, bool last, size_t total_bytes) {
    if (total_bytes != HOME_PROFILE_PHOTO_BYTES || (!data && length))
        return ESP_ERR_INVALID_ARG;
    SemaphoreHandle_t guard = lock();
    if (!guard) return ESP_ERR_NO_MEM;
    xSemaphoreTake(guard, portMAX_DELAY);
    esp_err_t err = mount_locked();
    if (err == ESP_OK && first) {
        abort_upload_locked();
        s_upload = fopen(kTmpPath, "wb");
        if (!s_upload) err = ESP_FAIL;
    }
    if (err == ESP_OK && (!s_upload || length > HOME_PROFILE_PHOTO_BYTES - s_written)) {
        abort_upload_locked();
        err = ESP_ERR_INVALID_SIZE;
    } else if (err == ESP_OK && length && fwrite(data, 1, length, s_upload) != length) {
        abort_upload_locked();
        err = ESP_FAIL;
    } else if (err == ESP_OK) {
        s_written += length;
    }
    if (err == ESP_OK && last) {
        if (s_written != HOME_PROFILE_PHOTO_BYTES) {
            abort_upload_locked();
            err = ESP_ERR_INVALID_SIZE;
        } else {
            const int flush_err = fflush(s_upload);
            const int close_err = fclose(s_upload);
            s_upload = NULL;
            if (flush_err || close_err) {
                abort_upload_locked();
                err = ESP_FAIL;
            } else {
                if (!file_has_expected_size(kTmpPath)) {
                    abort_upload_locked();
                    err = ESP_ERR_INVALID_SIZE;
                } else {
                    remove(kBackupPath);
                    if (access(kPath, F_OK) == 0 && rename(kPath, kBackupPath) != 0) {
                        abort_upload_locked();
                        err = ESP_FAIL;
                    } else if (rename(kTmpPath, kPath) != 0) {
                        rename(kBackupPath, kPath);
                        abort_upload_locked();
                        err = ESP_FAIL;
                    } else {
                        remove(kBackupPath);
                        ++s_version;
                        ESP_LOGI(TAG, "Profile photo stored: %u bytes",
                                 (unsigned)HOME_PROFILE_PHOTO_BYTES);
                    }
                }
            }
            s_written = 0;
        }
    }
    xSemaphoreGive(guard);
    return err;
}

esp_err_t home_photo_delete(void) {
    SemaphoreHandle_t guard = lock();
    if (!guard) return ESP_ERR_NO_MEM;
    xSemaphoreTake(guard, portMAX_DELAY);
    esp_err_t err = mount_locked();
    if (err == ESP_OK) {
        abort_upload_locked();
        remove(kBackupPath);
        if (remove(kPath) != 0 && errno != ENOENT) err = ESP_FAIL;
        if (err == ESP_OK) ++s_version;
    }
    xSemaphoreGive(guard);
    return err;
}
