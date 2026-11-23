// 首页个人资料:姓名、签名、城市与坐标保存在 NVS。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HOME_NAME_MAX 32      /* UTF-8 字节数，含结尾 0（约 9 个汉字） */
#define HOME_SIGNATURE_MAX 96 /* UTF-8 字节数，含结尾 0（约 31 个汉字） */
#define HOME_CITY_MAX 48      /* UTF-8 字节数，含结尾 0 */
#define HOME_ADDRESS_MAX 128
#define HOME_COORD_MAX 16     /* "39.9075" 字符串 */
#define HOME_REGION_MAX 32

typedef struct {
    char name[HOME_NAME_MAX];
    char signature[HOME_SIGNATURE_MAX];
    char city[HOME_CITY_MAX];
    char address[HOME_ADDRESS_MAX];
    char lat[HOME_COORD_MAX];
    char lon[HOME_COORD_MAX];
    char region[HOME_REGION_MAX];   /* shared weather/radio province */
    char radio_city[HOME_CITY_MAX]; /* parent city when a county is selected */
    char country_code[4];
} home_profile_t;

// Strict decimal coordinates; empty latitude AND longitude disable weather.
bool home_profile_coordinates_valid(const char *lat, const char *lon);

// Load text fields from NVS; no filesystem dependency.
void home_profile_init(void);

// Thread-safe copy for Home, radio, weather and setup HTTP handlers.
void home_profile_get(home_profile_t *out);

// Incremented after a successful profile update.
uint32_t home_profile_version(void);

// Save the complete text profile. Oversized input is rejected, never truncated.
esp_err_t home_profile_set_text(const home_profile_t *in);

#ifdef __cplusplus
}
#endif
