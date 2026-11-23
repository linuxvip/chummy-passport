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
static const char *const kScaledPath = HOME_PHOTO_BASE_PATH "/profile-photo.scaled";
static const char *const kBackupPath = HOME_PHOTO_BASE_PATH "/profile-photo.backup";

static StaticSemaphore_t s_mutex_storage;
static SemaphoreHandle_t s_mutex;
static bool s_mounted;
static FILE *s_upload;
static size_t s_written;
static size_t s_total_bytes;
static uint32_t s_version;
static const char *s_error_stage = "none";

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
    remove(kScaledPath);
    s_written = 0;
    s_total_bytes = 0;
}

static bool file_has_size(const char *path, size_t expected) {
    struct stat info = {0};
    return stat(path, &info) == 0 && info.st_size == (off_t)expected;
}

static bool file_has_expected_size(const char *path) {
    return file_has_size(path, HOME_PROFILE_PHOTO_BYTES);
}

static esp_err_t scale_legacy_upload_locked(void) {
    FILE *source = fopen(kTmpPath, "rb");
    if (!source) {
        s_error_stage = "legacy_scale_open";
        return ESP_FAIL;
    }
    FILE *target = fopen(kScaledPath, "wb");
    if (!target) {
        fclose(source);
        s_error_stage = "legacy_scale_create";
        return ESP_FAIL;
    }

    const size_t source_row = HOME_PROFILE_PHOTO_LEGACY_W * 2;
    uint8_t source_pixels[HOME_PROFILE_PHOTO_LEGACY_W * 2];
    uint8_t target_pixels[HOME_PROFILE_PHOTO_W * 2];
    bool ok = true;
    for (int y = 0; y < HOME_PROFILE_PHOTO_H && ok; ++y) {
        const int source_y = y * HOME_PROFILE_PHOTO_LEGACY_H / HOME_PROFILE_PHOTO_H;
        if (fseek(source, (long)(source_y * source_row), SEEK_SET) != 0 ||
            fread(source_pixels, 1, sizeof(source_pixels), source) != sizeof(source_pixels)) {
            ok = false;
            break;
        }
        for (int x = 0; x < HOME_PROFILE_PHOTO_W; ++x) {
            const int source_x = x * HOME_PROFILE_PHOTO_LEGACY_W / HOME_PROFILE_PHOTO_W;
            memcpy(target_pixels + x * 2, source_pixels + source_x * 2, 2);
        }
        if (fwrite(target_pixels, 1, sizeof(target_pixels), target) != sizeof(target_pixels))
            ok = false;
    }
    if (fclose(source) != 0) ok = false;
    if (fclose(target) != 0) ok = false;
    if (!ok || !file_has_expected_size(kScaledPath)) {
        remove(kScaledPath);
        s_error_stage = "legacy_scale_write";
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t commit_upload_locked(const char *source) {
    // SPIFFS can replace an existing object directly. This is the smallest
    // transaction and avoids a second rename on firmware that supports it.
    if (rename(source, kPath) == 0) {
        remove(kBackupPath);
        return ESP_OK;
    }
    s_error_stage = "commit_direct";
    remove(kBackupPath);
    if (access(kPath, F_OK) == 0 && rename(kPath, kBackupPath) != 0) {
        s_error_stage = "commit_backup";
        return ESP_FAIL;
    }
    if (rename(source, kPath) != 0) {
        s_error_stage = "commit_replace";
        rename(kBackupPath, kPath);
        return ESP_FAIL;
    }
    remove(kBackupPath);
    return ESP_OK;
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
        s_error_stage = "storage_mount";
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
    remove(kScaledPath);
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
    const bool supported_size = total_bytes == HOME_PROFILE_PHOTO_BYTES ||
        total_bytes == HOME_PROFILE_PHOTO_LEGACY_BYTES;
    if (!supported_size || (!data && length))
        return ESP_ERR_INVALID_ARG;
    SemaphoreHandle_t guard = lock();
    if (!guard) return ESP_ERR_NO_MEM;
    xSemaphoreTake(guard, portMAX_DELAY);
    esp_err_t err = mount_locked();
    if (err == ESP_OK && first) {
        abort_upload_locked();
        s_error_stage = "none";
        s_total_bytes = total_bytes;
        s_upload = fopen(kTmpPath, "wb");
        if (!s_upload) {
            s_error_stage = "tmp_open";
            err = ESP_FAIL;
        }
    }
    if (err == ESP_OK && total_bytes != s_total_bytes) {
        abort_upload_locked();
        s_error_stage = "chunk_total";
        err = ESP_ERR_INVALID_ARG;
    } else if (err == ESP_OK && (!s_upload || length > s_total_bytes - s_written)) {
        abort_upload_locked();
        s_error_stage = "chunk_size";
        err = ESP_ERR_INVALID_SIZE;
    } else if (err == ESP_OK && length && fwrite(data, 1, length, s_upload) != length) {
        abort_upload_locked();
        s_error_stage = "tmp_write";
        err = ESP_FAIL;
    } else if (err == ESP_OK) {
        s_written += length;
    }
    if (err == ESP_OK && last) {
        if (s_written != s_total_bytes) {
            abort_upload_locked();
            s_error_stage = "upload_size";
            err = ESP_ERR_INVALID_SIZE;
        } else {
            const int flush_err = fflush(s_upload);
            const int close_err = fclose(s_upload);
            s_upload = NULL;
            if (flush_err || close_err) {
                abort_upload_locked();
                s_error_stage = "tmp_close";
                err = ESP_FAIL;
            } else {
                if (!file_has_size(kTmpPath, s_total_bytes)) {
                    abort_upload_locked();
                    s_error_stage = "tmp_stat";
                    err = ESP_ERR_INVALID_SIZE;
                } else {
                    const char *source = kTmpPath;
                    if (s_total_bytes == HOME_PROFILE_PHOTO_LEGACY_BYTES) {
                        err = scale_legacy_upload_locked();
                        if (err == ESP_OK) source = kScaledPath;
                    }
                    if (err != ESP_OK) {
                        abort_upload_locked();
                    } else if (commit_upload_locked(source) != ESP_OK) {
                        abort_upload_locked();
                        err = ESP_FAIL;
                    } else {
                        s_error_stage = "none";
                        ++s_version;
                        ESP_LOGI(TAG, "Profile photo stored: %u bytes",
                                 (unsigned)HOME_PROFILE_PHOTO_BYTES);
                    }
                }
            }
            s_written = 0;
            s_total_bytes = 0;
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

const char *home_photo_last_error(void) {
    return s_error_stage ? s_error_stage : "unknown";
}
