#include "apps/home/home_profile.h"

#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "home_profile";

#define NVS_NAMESPACE "home_profile"

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static home_profile_t s_profile;
static uint32_t s_version = 1;

static void apply_default_city(void) {
    snprintf(s_profile.city, sizeof(s_profile.city), "北京");
    snprintf(s_profile.address, sizeof(s_profile.address), "北京市");
    snprintf(s_profile.lat, sizeof(s_profile.lat), "39.9042");
    snprintf(s_profile.lon, sizeof(s_profile.lon), "116.4074");
    snprintf(s_profile.region, sizeof(s_profile.region), "北京");
    snprintf(s_profile.radio_city, sizeof(s_profile.radio_city), "北京");
    snprintf(s_profile.country_code, sizeof(s_profile.country_code), "CN");
}

static bool utf8_len_ok(const char *s, size_t cap) {
    return s && strnlen(s, cap) < cap;
}

static bool coordinate_valid(const char *s, double min, double max) {
    if (!s || !*s || strnlen(s, HOME_COORD_MAX) >= HOME_COORD_MAX) return false;
    const char *p = s;
    if (*p == '-' || *p == '+') ++p;
    bool digit = false, dot = false;
    for (; *p; ++p) {
        if (*p >= '0' && *p <= '9') digit = true;
        else if (*p == '.' && !dot) dot = true;
        else return false;
    }
    if (!digit) return false;
    const double n = strtod(s, NULL);
    return isfinite(n) && n >= min && n <= max;
}

bool home_profile_coordinates_valid(const char *lat, const char *lon) {
    return lat && lon && ((!lat[0] && !lon[0]) ||
        (coordinate_valid(lat, -90, 90) && coordinate_valid(lon, -180, 180)));
}

void home_profile_init(void) {
    memset(&s_profile, 0, sizeof(s_profile));
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        apply_default_city();
        ESP_LOGI(TAG, "No profile stored yet, using default city Beijing");
        return;
    }
    size_t len;
    len = sizeof(s_profile.name);
    nvs_get_str(handle, "name", s_profile.name, &len);
    len = sizeof(s_profile.signature);
    nvs_get_str(handle, "signature", s_profile.signature, &len);
    len = sizeof(s_profile.city);
    nvs_get_str(handle, "city", s_profile.city, &len);
    len = sizeof(s_profile.address);
    nvs_get_str(handle, "address", s_profile.address, &len);
    len = sizeof(s_profile.lat);
    nvs_get_str(handle, "lat", s_profile.lat, &len);
    len = sizeof(s_profile.lon);
    nvs_get_str(handle, "lon", s_profile.lon, &len);
    len = sizeof(s_profile.region);
    nvs_get_str(handle, "region", s_profile.region, &len);
    len = sizeof(s_profile.radio_city);
    nvs_get_str(handle, "radio_city", s_profile.radio_city, &len);
    len = sizeof(s_profile.country_code);
    nvs_get_str(handle, "country_code", s_profile.country_code, &len);
    char city_choice[2] = {0};
    len = sizeof(city_choice);
    const bool city_choice_saved =
        nvs_get_str(handle, "city_choice", city_choice, &len) == ESP_OK;
    nvs_close(handle);
    // Preserve an explicitly empty current choice while migrating old profiles.
    if (!city_choice_saved && !s_profile.city[0]) apply_default_city();
    ESP_LOGI(TAG, "Profile loaded: name='%s' city='%s' lat='%s' lon='%s'",
             s_profile.name, s_profile.city, s_profile.lat, s_profile.lon);
}

void home_profile_get(home_profile_t *out) {
    if (!out) return;
    portENTER_CRITICAL(&s_mux);
    *out = s_profile;
    portEXIT_CRITICAL(&s_mux);
}

uint32_t home_profile_version(void) {
    portENTER_CRITICAL(&s_mux);
    uint32_t version = s_version;
    portEXIT_CRITICAL(&s_mux);
    return version;
}

esp_err_t home_profile_set_text(const home_profile_t *in) {
    if (!in) return ESP_ERR_INVALID_ARG;
    if (!utf8_len_ok(in->name, HOME_NAME_MAX) ||
        !utf8_len_ok(in->signature, HOME_SIGNATURE_MAX) ||
        !utf8_len_ok(in->city, HOME_CITY_MAX) ||
        !utf8_len_ok(in->address, HOME_ADDRESS_MAX) ||
        !utf8_len_ok(in->lat, HOME_COORD_MAX) ||
        !utf8_len_ok(in->lon, HOME_COORD_MAX) ||
        !utf8_len_ok(in->region, HOME_REGION_MAX) ||
        !utf8_len_ok(in->radio_city, HOME_CITY_MAX) ||
        !utf8_len_ok(in->country_code, sizeof(in->country_code))) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (!home_profile_coordinates_valid(in->lat, in->lon) ||
        (in->country_code[0] && strcmp(in->country_code, "CN") &&
         strcmp(in->country_code, "HK") && strcmp(in->country_code, "MO") &&
         strcmp(in->country_code, "TW"))) return ESP_ERR_INVALID_ARG;

    // Persist first so UI state always matches NVS.
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }
    do {
        if ((err = nvs_set_str(handle, "name", in->name)) != ESP_OK) break;
        if ((err = nvs_set_str(handle, "signature", in->signature)) != ESP_OK) break;
        if ((err = nvs_set_str(handle, "city", in->city)) != ESP_OK) break;
        if ((err = nvs_set_str(handle, "address", in->address)) != ESP_OK) break;
        if ((err = nvs_set_str(handle, "lat", in->lat)) != ESP_OK) break;
        if ((err = nvs_set_str(handle, "lon", in->lon)) != ESP_OK) break;
        if ((err = nvs_set_str(handle, "region", in->region)) != ESP_OK) break;
        if ((err = nvs_set_str(handle, "radio_city", in->radio_city)) != ESP_OK) break;
        if ((err = nvs_set_str(handle, "country_code", in->country_code)) != ESP_OK) break;
        if ((err = nvs_set_str(handle, "city_choice", "1")) != ESP_OK) break;
        err = nvs_commit(handle);
    } while (false);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Profile save failed: %s", esp_err_to_name(err));
        return err;
    }

    portENTER_CRITICAL(&s_mux);
    s_profile = *in;
    s_version++;
    portEXIT_CRITICAL(&s_mux);
    ESP_LOGI(TAG, "Profile updated: name='%s' city='%s'", in->name, in->city);
    return ESP_OK;
}
