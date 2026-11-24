#include "wifi_configuration_ap.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <algorithm>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <lwip/ip_addr.h>
#include <nvs.h>
#include <nvs_flash.h>
#include "ssid_manager.h"
#include "sdkconfig.h"

#define TAG "WifiConfigurationAp"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

WifiConfigurationAp::WifiConfigurationAp()
{
    event_group_ = xEventGroupCreate();
    instance_any_id_ = nullptr;
    instance_got_ip_ = nullptr;
    max_tx_power_ = 0;
}

std::vector<wifi_ap_record_t> WifiConfigurationAp::GetAccessPoints()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return ap_records_;
}

WifiConfigurationAp::~WifiConfigurationAp()
{
    Stop();
    if (event_group_) {
        vEventGroupDelete(event_group_);
        event_group_ = nullptr;
    }
}

void WifiConfigurationAp::SetSsidPrefix(const std::string &&ssid_prefix)
{
    ssid_prefix_ = ssid_prefix;
}

void WifiConfigurationAp::SetSsidPrefix(const std::string &ssid_prefix)
{
    ssid_prefix_ = ssid_prefix;
}

void WifiConfigurationAp::Start(bool device_only)
{
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &WifiConfigurationAp::WifiEventHandler,
                                                        this,
                                                        &instance_any_id_));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &WifiConfigurationAp::IpEventHandler,
                                                        this,
                                                        &instance_got_ip_));

    if (device_only) {
        if (!esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")) esp_netif_create_default_wifi_sta();
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
    } else {
        StartAccessPoint();
    }

    // Setup periodic WiFi scan timer before triggering scan to prevent null timer race condition
    esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            auto* self = static_cast<WifiConfigurationAp*>(arg);
            if (!self->is_connecting_) {
                self->StartScan();
            }
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "wifi_scan_timer",
        .skip_unhandled_events = false
    };
    if (scan_timer_ == nullptr) {
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &scan_timer_));
    }

    // Start scan immediately
    StartScan();
}

void WifiConfigurationAp::StartScan() {
    wifi_scan_config_t config = {};
    config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    config.scan_time.active.min = 20;
    config.scan_time.active.max = 80;
    config.home_chan_dwell_time = 40;
    // Keep the captive AP responsive between channels; never block HTTP handlers.
    const esp_err_t err = esp_wifi_scan_start(&config, false);
    if (err != ESP_OK && scan_timer_) {
        ESP_LOGW(TAG, "Background scan deferred: %s", esp_err_to_name(err));
        esp_timer_start_once(scan_timer_, 5000000);
    }
}

std::string WifiConfigurationAp::GetSsid()
{
    if (!fixed_ssid_.empty()) {
        // IEEE 802.11 SSIDs are limited to 32 bytes. The caller owns the UTF-8
        // content; truncate defensively so copying into wifi_config_t is safe.
        return fixed_ssid_.substr(0, 32);
    }
    // Get MAC and use it to generate a unique SSID
    uint8_t mac[6];
#if CONFIG_IDF_TARGET_ESP32P4
    esp_wifi_get_mac(WIFI_IF_AP, mac);
#else
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));
#endif
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "%s-%02X%02X", ssid_prefix_.c_str(), mac[4], mac[5]);
    return std::string(ssid);
}

void WifiConfigurationAp::StartAccessPoint()
{
    // Note: esp_netif_init() and esp_wifi_init() should be called once before calling this method
    // WiFi driver is initialized by WifiManager::Initialize() and kept alive

    // Create the default WiFi AP interface
    ap_netif_ = esp_netif_create_default_wifi_ap();

    // In APSTA mode, ensure default station netif exists
    if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == nullptr) {
        esp_netif_create_default_wifi_sta();
    }

    // Set the router IP address to 192.168.4.1
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    esp_netif_dhcps_stop(ap_netif_);
    esp_netif_set_ip_info(ap_netif_, &ip_info);
    esp_netif_dhcps_start(ap_netif_);

    // Start the DNS server
    dns_server_ = std::make_unique<DnsServer>();
    dns_server_->Start(ip_info.gw);

    // Get the SSID
    std::string ssid = GetSsid();

    // Set the WiFi configuration
    wifi_config_t wifi_config = {};
    strcpy((char *)wifi_config.ap.ssid, ssid.c_str());
    wifi_config.ap.ssid_len = ssid.length();
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;

    // Start the WiFi Access Point
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());

#ifdef CONFIG_SOC_WIFI_SUPPORT_5G
    ESP_ERROR_CHECK(esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO));
#else
    ESP_ERROR_CHECK(esp_wifi_set_band_mode(WIFI_BAND_MODE_2G_ONLY));
#endif

    ESP_LOGI(TAG, "Access Point started with SSID %s (open)", ssid.c_str());

    // 加载高级配置
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wifi", NVS_READONLY, &nvs);
    if (err == ESP_OK) {
        // 读取WiFi功率
        err = nvs_get_i8(nvs, "max_tx_power", &max_tx_power_);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "WiFi max tx power from NVS: %d", max_tx_power_);
            ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(max_tx_power_));
        } else {
            esp_wifi_get_max_tx_power(&max_tx_power_);
        }

        nvs_close(nvs);
    }
}

bool WifiConfigurationAp::ConnectToWifi(const std::string &ssid, const std::string &password, bool keep_connected)
{
    if (ssid.empty()) {
        ESP_LOGE(TAG, "SSID cannot be empty");
        return false;
    }

    if (ssid.length() > 32) {  // WiFi SSID 最大长度
        ESP_LOGE(TAG, "SSID too long");
        return false;
    }

    if (password.length() > 64) {
        ESP_LOGE(TAG, "Password too long");
        return false;
    }

    is_connecting_ = true;
    last_disconnect_reason_ = 0;
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));

    // Upper-level retry loop with delay between attempts.
    //
    // Background: in APSTA mode the captive-portal session is on the AP
    // beacon channel (typically 1), and the target home AP is on some
    // other channel (e.g. 10). When ConnectToWifi triggers, esp-wifi
    // performs a Channel Switch Announcement to move both AP and STA to
    // the home AP's channel, then immediately issues an association
    // request. The home AP frequently responds with "Association
    // Response status=30 (Refused Temporarily)" + a Comeback Time in
    // TUs (= ~1.1s for Buffalo routers, observed) because its own
    // state hasn't settled yet for the new station.
    //
    // The ESP-IDF wifi driver's failure_retry_cnt issues re-association
    // attempts back-to-back (within a few ms) and does not honor the
    // 802.11 Comeback Time, so every driver-internal retry is refused
    // the same way and the first ConnectToWifi() call returns failure.
    // By the time the user clicks "submit" a second time (~8s later)
    // the AP has fully settled and association succeeds on the first
    // try — which is why users observe "it always fails the first
    // time, then works".
    //
    // Fix: when an attempt fails, wait long enough for the comeback
    // timer + AP state settle (~3s is safe), then retry once. This
    // produces a single user-visible success path instead of forcing
    // the user to resubmit. The driver-internal retries (set below
    // via failure_retry_cnt) are kept as a secondary safety net.
    constexpr int kMaxAttempts = 2;
    constexpr int kRetryDelayMs = 3000;
    bool connected = false;

    for (int attempt = 1; attempt <= kMaxAttempts && !connected; ++attempt) {
        if (attempt > 1) {
            ESP_LOGI(TAG,
                "WiFi attempt %d/%d after %d ms delay "
                "(waiting for AP comeback timer + state settle)",
                attempt, kMaxAttempts, kRetryDelayMs);
            vTaskDelay(pdMS_TO_TICKS(kRetryDelayMs));
        }

        xEventGroupClearBits(event_group_, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
        esp_wifi_scan_stop();

        wifi_config_t wifi_config;
        bzero(&wifi_config, sizeof(wifi_config));
        memcpy(wifi_config.sta.ssid, ssid.data(), ssid.size());
        memcpy(wifi_config.sta.password, password.data(), password.size());
        wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        wifi_config.sta.failure_retry_cnt = 1;

        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        auto ret = esp_wifi_connect();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG,
                "esp_wifi_connect failed: %d (attempt %d/%d)",
                ret, attempt, kMaxAttempts);
            continue;
        }
        ESP_LOGI(TAG, "Connecting to WiFi %s (attempt %d/%d)",
            ssid.c_str(), attempt, kMaxAttempts);

        // Wait for the connection to complete for 10 or 25 seconds.
        EventBits_t bits = xEventGroupWaitBits(
            event_group_,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdTRUE,
            pdFALSE,
#ifdef CONFIG_SOC_WIFI_SUPPORT_5G
            pdMS_TO_TICKS(25000)
#else
            pdMS_TO_TICKS(10000)
#endif
        );

        if (bits & WIFI_CONNECTED_BIT) {
            connected = true;
        } else {
            const bool timed_out = (bits == 0);
            if (timed_out) {
                // Timeout — neither WIFI_CONNECTED_BIT nor WIFI_FAIL_BIT
                // was set, so WIFI_EVENT_STA_DISCONNECTED has not fired
                // and the driver may still be in `connecting` state.
                // Cancel the in-flight attempt explicitly before the
                // retry delay; without this the next esp_wifi_connect()
                // can return ESP_ERR_WIFI_STATE on a connecting-state
                // driver (per esp_wifi.h attention 3), making the retry
                // a no-op on slow / event-dropping APs.
                esp_wifi_disconnect();
            }
            ESP_LOGW(TAG,
                "Attempt %d/%d %s%s",
                attempt, kMaxAttempts,
                timed_out ? "timed out (driver may still be connecting)" : "failed",
                attempt < kMaxAttempts ? " — will retry" : "");
        }
    }
    is_connecting_ = false;

    if (connected) {
        ESP_LOGI(TAG, "Connected to WiFi %s", ssid.c_str());
        if (!keep_connected) esp_wifi_disconnect();
        return true;
    } else {
        ESP_LOGE(TAG,
            "Failed to connect to WiFi %s after %d attempts",
            ssid.c_str(), kMaxAttempts);
        return false;
    }
}

void WifiConfigurationAp::Save(const std::string &ssid, const std::string &password)
{
    ESP_LOGI(TAG, "Save SSID %s %d", ssid.c_str(), ssid.length());
    SsidManager::GetInstance().AddSsid(ssid, password);
}

void WifiConfigurationAp::WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    WifiConfigurationAp* self = static_cast<WifiConfigurationAp*>(arg);
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Station " MACSTR " joined, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Station " MACSTR " left, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
        // Association alone is insufficient: wait for DHCP in IpEventHandler.
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        self->last_disconnect_reason_ = static_cast<wifi_event_sta_disconnected_t *>(event_data)->reason;
        xEventGroupSetBits(self->event_group_, WIFI_FAIL_BIT);
    } else if (event_id == WIFI_EVENT_SCAN_DONE) {
        std::lock_guard<std::mutex> lock(self->mutex_);
        uint16_t ap_num = 0;
        esp_wifi_scan_get_ap_num(&ap_num);

        if (ap_num > 0) {
            ap_num = std::min<uint16_t>(ap_num, 20);
            self->ap_records_.resize(ap_num);
            esp_wifi_scan_get_ap_records(&ap_num, self->ap_records_.data());
            self->ap_records_.resize(ap_num);
        } else {
            self->ap_records_.clear();
        }

        // Avoid interrupting phone uploads with frequent off-channel scans.
        if (self->scan_timer_ != nullptr) {
            esp_timer_start_once(self->scan_timer_, 30 * 1000000);
        }
    }
}

void WifiConfigurationAp::IpEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    WifiConfigurationAp* self = static_cast<WifiConfigurationAp*>(arg);
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(self->event_group_, WIFI_CONNECTED_BIT);
    }
}

void WifiConfigurationAp::Stop() {
    // 停止定时器
    if (scan_timer_) {
        esp_timer_stop(scan_timer_);
        esp_timer_delete(scan_timer_);
        scan_timer_ = nullptr;
    }

    // 停止DNS服务器
    if (dns_server_) {
        dns_server_->Stop();
        dns_server_.reset();
    }

    // 注销事件处理器
    if (instance_any_id_) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id_);
        instance_any_id_ = nullptr;
    }
    if (instance_got_ip_) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip_);
        instance_got_ip_ = nullptr;
    }

    // 停止WiFi（但不 deinit，WiFi 驱动由 WifiManager 管理）
    esp_wifi_stop();

    // 销毁网络接口
    if (ap_netif_) {
        esp_netif_destroy_default_wifi(ap_netif_);
        ap_netif_ = nullptr;
    }

    ESP_LOGI(TAG, "Wifi configuration AP stopped");
    std::vector<wifi_ap_record_t>().swap(ap_records_);
}
