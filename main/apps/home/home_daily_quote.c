#include "apps/home/home_daily_quote.h"

#include <ctype.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#if defined(CONFIG_HOME_DAILY_QUOTE_URL)
#define QUOTE_URL CONFIG_HOME_DAILY_QUOTE_URL
#else
#define QUOTE_URL "https://v1.hitokoto.cn/?c=k&min_length=8&max_length=24&encode=json&charset=utf-8"
#endif

#if defined(CONFIG_HOME_DAILY_QUOTE_FALLBACK_URL)
#define QUOTE_FALLBACK_URL CONFIG_HOME_DAILY_QUOTE_FALLBACK_URL
#else
#define QUOTE_FALLBACK_URL "https://api.xygeng.cn/one"
#endif

static const char *TAG = "home_quote";
#define NVS_NAMESPACE "home_profile"
#define QUOTE_CACHE_VERSION 2
#define RESPONSE_MAX 1536
#define REQUEST_TIMEOUT_MS 5000
#define RETRY_DELAY_MS 120000

static portMUX_TYPE s_quote_mux = portMUX_INITIALIZER_UNLOCKED;
static home_daily_quote_t s_quote;
static uint32_t s_version;
static uint32_t s_attempt_day;
static int64_t s_retry_at_ms;
static TaskHandle_t s_task;
static atomic_bool s_busy;
static atomic_bool s_cancel;
static atomic_bool s_force_refresh;
static atomic_uint s_manual_round;

typedef struct {
    char body[RESPONSE_MAX];
    size_t used;
    bool overflow;
} response_t;

static const char *const kCuratedQuotes[] = {
    "行到水穷处，坐看云起时。",
    "知人者智，自知者明。",
    "合抱之木，生于毫末。",
    "静以修身，俭以养德。",
    "山重水复疑无路，柳暗花明又一村。",
    "心有所信，方能行远。",
    "见天地，见众生，见自己。",
    "君子和而不同，小人同而不和。",
    "千里之行，始于足下。",
    "知足不辱，知止不殆。",
    "路虽远，行则将至。",
    "事虽难，做则必成。",
};

static size_t utf8_length(const char *text) {
    size_t count = 0;
    for (const unsigned char *p = (const unsigned char *)text; p && *p; ++p)
        if ((*p & 0xc0U) != 0x80U) ++count;
    return count;
}

static void copy_clean(char *out, size_t capacity, const char *input) {
    size_t used = 0;
    bool pending_space = false;
    for (const unsigned char *p = (const unsigned char *)input; *p && used + 1 < capacity; ++p) {
        if (isspace(*p) || *p < 0x20U) {
            pending_space = used > 0;
            continue;
        }
        if (pending_space) {
            if (used + 1 < capacity) out[used++] = ' ';
            pending_space = false;
        }
        out[used++] = (char)*p;
    }
    while (used && out[used - 1] == ' ') --used;
    out[used] = '\0';
}

static void persist(const home_daily_quote_t *quote) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    if (nvs_set_u8(handle, "dqver", QUOTE_CACHE_VERSION) == ESP_OK &&
        nvs_set_u32(handle, "dqday", quote->local_day) == ESP_OK &&
        nvs_set_str(handle, "dqtext", quote->text) == ESP_OK &&
        nvs_set_str(handle, "dqsource", quote->source) == ESP_OK) {
        nvs_commit(handle);
    }
    nvs_close(handle);
}

static esp_err_t use_curated_quote(uint32_t local_day) {
    const size_t count = sizeof(kCuratedQuotes) / sizeof(kCuratedQuotes[0]);
    home_daily_quote_t next = {.local_day = local_day};
    const unsigned selection = local_day + atomic_load(&s_manual_round);
    snprintf(next.text, sizeof(next.text), "%s", kCuratedQuotes[selection % count]);
    snprintf(next.source, sizeof(next.source), "精选");
    portENTER_CRITICAL(&s_quote_mux);
    s_quote = next;
    ++s_version;
    portEXIT_CRITICAL(&s_quote_mux);
    persist(&next);
    ESP_LOGI(TAG, "Quote uses curated fallback for %lu", (unsigned long)local_day);
    return ESP_OK;
}

void home_daily_quote_init(void) {
    home_daily_quote_t loaded = {0};
    nvs_handle_t handle;
    size_t text_size = sizeof(loaded.text);
    size_t source_size = sizeof(loaded.source);
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        uint8_t cache_version = 0;
        if (nvs_get_u8(handle, "dqver", &cache_version) == ESP_OK &&
            cache_version == QUOTE_CACHE_VERSION &&
            nvs_get_u32(handle, "dqday", &loaded.local_day) == ESP_OK &&
            nvs_get_str(handle, "dqtext", loaded.text, &text_size) == ESP_OK &&
            loaded.text[0]) {
            if (nvs_get_str(handle, "dqsource", loaded.source, &source_size) != ESP_OK)
                snprintf(loaded.source, sizeof(loaded.source), "网络");
            portENTER_CRITICAL(&s_quote_mux);
            s_quote = loaded;
            portEXIT_CRITICAL(&s_quote_mux);
        }
        nvs_close(handle);
    }
    atomic_store(&s_busy, false);
    atomic_store(&s_cancel, false);
    atomic_store(&s_force_refresh, false);
    atomic_store(&s_manual_round, 0);
    s_task = NULL;
    s_attempt_day = 0;
    s_retry_at_ms = 0;
}

bool home_daily_quote_get(home_daily_quote_t *out) {
    if (!out) return false;
    portENTER_CRITICAL(&s_quote_mux);
    *out = s_quote;
    const bool valid = out->local_day && out->text[0];
    portEXIT_CRITICAL(&s_quote_mux);
    return valid;
}

uint32_t home_daily_quote_version(void) {
    portENTER_CRITICAL(&s_quote_mux);
    const uint32_t version = s_version;
    portEXIT_CRITICAL(&s_quote_mux);
    return version;
}

static esp_err_t on_data(esp_http_client_event_t *event) {
    response_t *response = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) return ESP_OK;
    if ((size_t)event->data_len > sizeof(response->body) - 1 - response->used) {
        response->overflow = true;
        return ESP_FAIL;
    }
    memcpy(response->body + response->used, event->data, (size_t)event->data_len);
    response->used += (size_t)event->data_len;
    return ESP_OK;
}

static esp_err_t fetch_once(const char *url, uint32_t local_day) {
    response_t response = {0};
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 1000,
        .buffer_size = 512,
        .buffer_size_tx = 256,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = on_data,
        .user_data = &response,
        .is_async = true,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;
    esp_http_client_set_header(client, "User-Agent", "PassportOS/0.2");
    esp_http_client_set_header(client, "Accept", "application/json");

    const int64_t deadline = esp_timer_get_time() / 1000 + REQUEST_TIMEOUT_MS;
    esp_err_t err = ESP_ERR_HTTP_EAGAIN;
    while (!atomic_load(&s_cancel) && esp_timer_get_time() / 1000 < deadline &&
           !response.overflow) {
        err = esp_http_client_perform(client);
        if (err != ESP_ERR_HTTP_EAGAIN) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    const int status = esp_http_client_get_status_code(client);
    const bool complete = esp_http_client_is_complete_data_received(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status != 200 || !complete || !response.used ||
        response.overflow || atomic_load(&s_cancel)) {
        ESP_LOGW(TAG, "quote request failed: HTTP=%d len=%u err=%d",
                 status, (unsigned)response.used, (int)err);
        return ESP_FAIL;
    }
    response.body[response.used] = '\0';

    cJSON *root = cJSON_Parse(response.body);
    if (!root) return ESP_FAIL;
    const cJSON *text = cJSON_GetObjectItemCaseSensitive(root, "hitokoto");
    const cJSON *from = cJSON_GetObjectItemCaseSensitive(root, "from");
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsString(text) && cJSON_IsObject(data))
        text = cJSON_GetObjectItemCaseSensitive(data, "content");
    if (!cJSON_IsString(from) && cJSON_IsObject(data)) {
        from = cJSON_GetObjectItemCaseSensitive(data, "name");
        if (!cJSON_IsString(from)) from = cJSON_GetObjectItemCaseSensitive(data, "origin");
    }
    home_daily_quote_t next = {.local_day = local_day};
    if (cJSON_IsString(text)) {
        copy_clean(next.text, sizeof(next.text), text->valuestring);
    }
    if (cJSON_IsString(from)) {
        copy_clean(next.source, sizeof(next.source), from->valuestring);
    }
    cJSON_Delete(root);
    const size_t length = utf8_length(next.text);
    if (length < 8 || length > 24) {
        ESP_LOGW(TAG, "Quote rejected: length=%u", (unsigned)length);
        return ESP_FAIL;
    }
    if (!next.source[0]) snprintf(next.source, sizeof(next.source), "网络");

    portENTER_CRITICAL(&s_quote_mux);
    s_quote = next;
    ++s_version;
    portEXIT_CRITICAL(&s_quote_mux);
    persist(&next);
    ESP_LOGI(TAG, "Quote cached for %lu: %s", (unsigned long)local_day, next.source);
    return ESP_OK;
}

static esp_err_t fetch(uint32_t local_day) {
    if (fetch_once(QUOTE_URL, local_day) == ESP_OK) return ESP_OK;
    if (atomic_load(&s_cancel)) return ESP_FAIL;
    ESP_LOGW(TAG, "Primary quote endpoint failed; trying fallback");
    if (fetch_once(QUOTE_FALLBACK_URL, local_day) == ESP_OK) return ESP_OK;
    if (atomic_load(&s_cancel)) return ESP_FAIL;
    return use_curated_quote(local_day);
}

static void quote_task(void *arg) {
    const uint32_t local_day = (uint32_t)(uintptr_t)arg;
    const esp_err_t result = fetch(local_day);
    if (result != ESP_OK) {
        s_retry_at_ms = esp_timer_get_time() / 1000 + RETRY_DELAY_MS;
    }
    s_task = NULL;
    atomic_store(&s_busy, false);
    vTaskDelete(NULL);
}

void home_daily_quote_schedule(uint32_t local_day) {
    if (!local_day || atomic_load(&s_busy)) return;
    const bool force = atomic_exchange(&s_force_refresh, false);
    home_daily_quote_t current;
    if (!force && home_daily_quote_get(&current) && current.local_day == local_day) return;
    const int64_t now = esp_timer_get_time() / 1000;
    if (!force && s_attempt_day == local_day && now < s_retry_at_ms) return;

    s_attempt_day = local_day;
    s_retry_at_ms = now + REQUEST_TIMEOUT_MS + 1000;
    atomic_store(&s_cancel, false);
    atomic_store(&s_busy, true);
    if (xTaskCreate(quote_task, "daily_quote", 8192, (void *)(uintptr_t)local_day, 3, &s_task) != pdPASS) {
        atomic_store(&s_busy, false);
        s_task = NULL;
        ESP_LOGW(TAG, "Failed to create quote task");
    }
}

void home_daily_quote_request_refresh(uint32_t local_day) {
    if (!local_day) return;
    atomic_fetch_add(&s_manual_round, 1);
    atomic_store(&s_force_refresh, true);
    s_attempt_day = 0;
    s_retry_at_ms = 0;
}

void home_daily_quote_cancel_requests(void) {
    atomic_store(&s_cancel, true);
}

bool home_daily_quote_busy(void) {
    return atomic_load(&s_busy);
}

void home_daily_quote_stop(void) {
    atomic_store(&s_cancel, true);
    s_task = NULL;
}
