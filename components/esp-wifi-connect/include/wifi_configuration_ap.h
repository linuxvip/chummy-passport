#ifndef _WIFI_CONFIGURATION_AP_H_
#define _WIFI_CONFIGURATION_AP_H_

#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <atomic>

#include <esp_event.h>
#include <esp_timer.h>
#include <esp_netif.h>
#include <esp_wifi_types_generic.h>

#include "dns_server.h"
#include "sdkconfig.h"

/**
 * WifiConfigurationAp - WiFi configuration access point
 *
 * Creates a WiFi hotspot with a captive portal for configuring WiFi credentials.
 * Note: WiFi driver must be initialized before using this class.
 */
class WifiConfigurationAp {
public:
    WifiConfigurationAp();
    ~WifiConfigurationAp();

    // Delete copy constructor and assignment operator
    WifiConfigurationAp(const WifiConfigurationAp&) = delete;
    WifiConfigurationAp& operator=(const WifiConfigurationAp&) = delete;

    void SetSsidPrefix(const std::string &&ssid_prefix);
    void SetSsidPrefix(const std::string &ssid_prefix);
    void SetFixedSsid(const std::string &ssid) { fixed_ssid_ = ssid; }
    void Start(bool device_only = false);
    int LastDisconnectReason() const { return last_disconnect_reason_.load(); }
    void Stop();
    bool ConnectToWifi(const std::string &ssid, const std::string &password, bool keep_connected = false);
    void Save(const std::string &ssid, const std::string &password);
    std::vector<wifi_ap_record_t> GetAccessPoints();
    std::string GetSsid();

private:
    std::mutex mutex_;
    std::unique_ptr<DnsServer> dns_server_;
    EventGroupHandle_t event_group_;
    std::string ssid_prefix_;
    std::string fixed_ssid_;
    esp_event_handler_instance_t instance_any_id_;
    esp_event_handler_instance_t instance_got_ip_;
    esp_timer_handle_t scan_timer_ = nullptr;
    bool is_connecting_ = false;
    std::atomic<int> last_disconnect_reason_{0};
    esp_netif_t* ap_netif_ = nullptr;
    std::vector<wifi_ap_record_t> ap_records_;

    int8_t max_tx_power_;

    void StartAccessPoint();
    void StartScan();

    // Event handlers
    static void WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void IpEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
};

#endif // _WIFI_CONFIGURATION_AP_H_
