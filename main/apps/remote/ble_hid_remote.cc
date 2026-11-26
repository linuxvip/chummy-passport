#include "apps/remote/ble_hid_remote.h"
#include "apps/remote/hid_reports.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

extern "C" void ble_store_config_init(void);

namespace remote {
namespace {

constexpr const char *kTag = "remote_hid";
constexpr size_t kQueueDepth = 8;
constexpr size_t kWorkerStackBytes = 6144;
constexpr int kWorkerPriority = 5;
constexpr int kKeyHoldMs = 20;
constexpr int kActionGapMs = 100;
constexpr uint8_t kReportTypeInput = 1;
constexpr uint16_t kKeyboardAppearance = 0x03C1;

enum class CommandType : uint8_t {
    Action,
    Retry,
    Quit,
};

struct Command {
    CommandType type;
    Action action;
};

std::atomic<BleState> s_state{BleState::Off};
std::atomic<Mode> s_mode{Mode::Media};
std::atomic<bool> s_connected{false};
std::atomic<bool> s_advertising{false};
std::atomic<uint32_t> s_passkey{0};
std::atomic<bool> s_stopping{false};

uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
bool s_host_started = false;
bool s_keyboard_subscribed = false;
bool s_consumer_subscribed = false;

QueueHandle_t s_queue = nullptr;
TaskHandle_t s_task = nullptr;
SemaphoreHandle_t s_done = nullptr;
StaticQueue_t s_queue_buffer{};
uint8_t s_queue_storage[kQueueDepth * sizeof(Command)]{};
StaticTask_t s_task_buffer{};
StackType_t s_task_stack[kWorkerStackBytes / sizeof(StackType_t)]{};

char s_device_name[24]{};
uint8_t s_random_addr[6]{};
uint8_t s_own_addr_type = BLE_OWN_ADDR_PUBLIC;
ble_uuid16_t s_hid_uuid = BLE_UUID16_INIT(0x1812);
ble_uuid16_t s_hid_info_uuid = BLE_UUID16_INIT(0x2A4A);
ble_uuid16_t s_report_map_uuid = BLE_UUID16_INIT(0x2A4B);
ble_uuid16_t s_control_point_uuid = BLE_UUID16_INIT(0x2A4C);
ble_uuid16_t s_report_uuid = BLE_UUID16_INIT(0x2A4D);
ble_uuid16_t s_protocol_mode_uuid = BLE_UUID16_INIT(0x2A4E);
ble_uuid16_t s_report_reference_uuid = BLE_UUID16_INIT(0x2908);
ble_hs_adv_fields s_adv_fields{};
int64_t s_last_action_us = 0;

constexpr uint8_t kHidInfo[] = {
    0x11, 0x01,  // HID 1.11
    0x00,        // non-localized country code
    0x03,        // normally connectable, remote wake
};
const uint8_t s_report_map_token = 0;
const uint8_t s_hid_info_token = 0;
const uint8_t s_control_point_token = 0;
const uint8_t s_protocol_mode_token = 0;
const uint8_t s_keyboard_report_token = 0;
const uint8_t s_consumer_report_token = 0;
const uint8_t s_keyboard_report_ref[] = {kKeyboardReportId, kReportTypeInput};
const uint8_t s_consumer_report_ref[] = {kConsumerReportId, kReportTypeInput};

uint8_t s_protocol_mode = 1;
uint8_t s_control_point = 0;
uint8_t s_keyboard_value[sizeof(KeyboardReport)]{};
uint8_t s_consumer_value[sizeof(ConsumerReport)]{};
uint16_t s_keyboard_report_handle = 0;
uint16_t s_consumer_report_handle = 0;

int append_value(os_mbuf *om, const void *data, size_t length) {
    return os_mbuf_append(om, data, static_cast<uint16_t>(length)) == 0
        ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

int report_reference_access(uint16_t, uint16_t,
                            ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_DSC) return BLE_ATT_ERR_UNLIKELY;
    return append_value(ctxt->om, arg, 2);
}

int hid_access(uint16_t, uint16_t, ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (arg == &s_report_map_token) {
            int rc = append_value(ctxt->om, kKeyboardReportMap,
                                  sizeof(kKeyboardReportMap));
            if (rc != 0) return rc;
            rc = append_value(ctxt->om, kConsumerReportMap,
                              sizeof(kConsumerReportMap));
            return rc;
        }
        if (arg == &s_hid_info_token)
            return append_value(ctxt->om, kHidInfo, sizeof(kHidInfo));
        if (arg == &s_protocol_mode_token)
            return append_value(ctxt->om, &s_protocol_mode, 1);
        if (arg == &s_control_point_token)
            return append_value(ctxt->om, &s_control_point, 1);
        if (arg == &s_keyboard_report_token)
            return append_value(ctxt->om, s_keyboard_value,
                                sizeof(s_keyboard_value));
        if (arg == &s_consumer_report_token)
            return append_value(ctxt->om, s_consumer_value,
                                sizeof(s_consumer_value));
    } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t value = 0;
        if (OS_MBUF_PKTLEN(ctxt->om) != 1 ||
            ble_hs_mbuf_to_flat(ctxt->om, &value, 1, nullptr) != 0)
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (arg == &s_protocol_mode_token) {
            if (value > 1) return BLE_ATT_ERR_UNLIKELY;
            s_protocol_mode = value;
            return 0;
        }
        if (arg == &s_control_point_token) {
            if (value > 1) return BLE_ATT_ERR_UNLIKELY;
            s_control_point = value;
            return 0;
        }
    }
    return BLE_ATT_ERR_UNLIKELY;
}

ble_gatt_dsc_def s_keyboard_descriptors[] = {
    {
        .uuid = &s_report_reference_uuid.u,
        .att_flags = BLE_ATT_F_READ | BLE_ATT_F_READ_ENC,
        .min_key_size = 16,
        .access_cb = report_reference_access,
        .arg = const_cast<uint8_t *>(s_keyboard_report_ref),
    },
    {},
};

ble_gatt_dsc_def s_consumer_descriptors[] = {
    {
        .uuid = &s_report_reference_uuid.u,
        .att_flags = BLE_ATT_F_READ | BLE_ATT_F_READ_ENC,
        .min_key_size = 16,
        .access_cb = report_reference_access,
        .arg = const_cast<uint8_t *>(s_consumer_report_ref),
    },
    {},
};

const ble_gatt_chr_def s_hid_characteristics[] = {
    {
        .uuid = &s_report_map_uuid.u,
        .access_cb = hid_access,
        .arg = const_cast<uint8_t *>(&s_report_map_token),
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
        .min_key_size = 16,
        .val_handle = nullptr,
        .cpfd = nullptr,
    },
    {
        .uuid = &s_hid_info_uuid.u,
        .access_cb = hid_access,
        .arg = const_cast<uint8_t *>(&s_hid_info_token),
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
        .min_key_size = 16,
        .val_handle = nullptr,
        .cpfd = nullptr,
    },
    {
        .uuid = &s_control_point_uuid.u,
        .access_cb = hid_access,
        .arg = const_cast<uint8_t *>(&s_control_point_token),
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC,
        .min_key_size = 16,
        .val_handle = nullptr,
        .cpfd = nullptr,
    },
    {
        .uuid = &s_protocol_mode_uuid.u,
        .access_cb = hid_access,
        .arg = const_cast<uint8_t *>(&s_protocol_mode_token),
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC |
                 BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC,
        .min_key_size = 16,
        .val_handle = nullptr,
        .cpfd = nullptr,
    },
    {
        .uuid = &s_report_uuid.u,
        .access_cb = hid_access,
        .arg = const_cast<uint8_t *>(&s_keyboard_report_token),
        .descriptors = s_keyboard_descriptors,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC |
                 BLE_GATT_CHR_F_NOTIFY,
        .min_key_size = 16,
        .val_handle = &s_keyboard_report_handle,
        .cpfd = nullptr,
    },
    {
        .uuid = &s_report_uuid.u,
        .access_cb = hid_access,
        .arg = const_cast<uint8_t *>(&s_consumer_report_token),
        .descriptors = s_consumer_descriptors,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC |
                 BLE_GATT_CHR_F_NOTIFY,
        .min_key_size = 16,
        .val_handle = &s_consumer_report_handle,
        .cpfd = nullptr,
    },
    {},
};

const ble_gatt_svc_def s_hid_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_hid_uuid.u,
        .includes = nullptr,
        .characteristics = s_hid_characteristics,
    },
    {},
};

void adv_init() {
    std::memset(&s_adv_fields, 0, sizeof(s_adv_fields));
    s_adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    s_adv_fields.appearance = kKeyboardAppearance;
    s_adv_fields.appearance_is_present = 1;
    s_adv_fields.name = reinterpret_cast<uint8_t *>(s_device_name);
    s_adv_fields.name_len = static_cast<uint8_t>(std::strlen(s_device_name));
    s_adv_fields.name_is_complete = 1;
    s_adv_fields.uuids16 = &s_hid_uuid;
    s_adv_fields.num_uuids16 = 1;
    s_adv_fields.uuids16_is_complete = 1;
}

int gap_event(ble_gap_event *event, void *arg);

esp_err_t start_advertising() {
    if (s_stopping.load(std::memory_order_acquire) ||
        s_connected.load(std::memory_order_acquire)) return ESP_OK;
    if (s_advertising.exchange(true, std::memory_order_acq_rel)) return ESP_OK;

    int rc = ble_gap_adv_set_fields(&s_adv_fields);
    if (rc == 0) {
        ble_gap_adv_params params{};
        params.conn_mode = BLE_GAP_CONN_MODE_UND;
        params.disc_mode = BLE_GAP_DISC_MODE_GEN;
        params.itvl_min = BLE_GAP_ADV_ITVL_MS(30);
        params.itvl_max = BLE_GAP_ADV_ITVL_MS(50);
        rc = ble_gap_adv_start(s_own_addr_type, nullptr, BLE_HS_FOREVER,
                               &params, gap_event, nullptr);
    }
    if (rc != 0) {
        s_advertising.store(false, std::memory_order_release);
        ESP_LOGW(kTag, "Advertising start failed: %d", rc);
        return ESP_FAIL;
    }
    s_state.store(BleState::Advertising, std::memory_order_release);
    return ESP_OK;
}

int gap_event(ble_gap_event *event, void *) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            s_advertising.store(false, std::memory_order_release);
            if (event->connect.status == 0) {
                s_conn_handle = event->connect.conn_handle;
                s_state.store(BleState::Pairing, std::memory_order_release);
                // Let the phone-owned connection flow initiate pairing. Starting
                // a peripheral security procedure here races some phone hosts
                // that already start pairing after the connection completes.
                // The encrypted HID attributes still require bonding before
                // any report can be read or subscribed.
            } else {
                s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
                s_state.store(BleState::Advertising, std::memory_order_release);
                start_advertising();
            }
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            s_connected.store(false, std::memory_order_release);
            s_keyboard_subscribed = false;
            s_consumer_subscribed = false;
            s_advertising.store(false, std::memory_order_release);
            s_state.store(BleState::Advertising, std::memory_order_release);
            start_advertising();
            return 0;

        case BLE_GAP_EVENT_ENC_CHANGE:
            if (event->enc_change.status == 0) {
                s_connected.store(true, std::memory_order_release);
                s_state.store(BleState::Connected, std::memory_order_release);
                ESP_LOGI(kTag, "Encrypted HID connection ready");
            } else {
                s_connected.store(false, std::memory_order_release);
                s_state.store(BleState::Pairing, std::memory_order_release);
                ESP_LOGW(kTag, "Encryption failed: %d", event->enc_change.status);
            }
            return 0;

        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == s_keyboard_report_handle)
                s_keyboard_subscribed = event->subscribe.cur_notify != 0;
            if (event->subscribe.attr_handle == s_consumer_report_handle)
                s_consumer_subscribed = event->subscribe.cur_notify != 0;
            return 0;

        case BLE_GAP_EVENT_PASSKEY_ACTION: {
            s_state.store(BleState::Pairing, std::memory_order_release);
            ble_sm_io io{};
            io.action = event->passkey.params.action;
            ESP_LOGI(kTag, "Pairing passkey action: %d", io.action);
            if (io.action == BLE_SM_IOACT_DISP) {
                io.passkey = s_passkey.load(std::memory_order_acquire);
            } else if (io.action == BLE_SM_IOACT_NUMCMP) {
                io.numcmp_accept = 1;
            } else {
                ESP_LOGW(kTag, "Unsupported passkey action: %d", io.action);
                return 0;
            }
            const int rc = ble_sm_inject_io(event->passkey.conn_handle, &io);
            if (rc != 0) ESP_LOGW(kTag, "Passkey injection failed: %d", rc);
            return 0;
        }

        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            ble_gap_conn_desc desc{};
            if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0)
                ble_store_util_delete_peer(&desc.peer_id_addr);
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }

        case BLE_GAP_EVENT_ADV_COMPLETE:
            s_advertising.store(false, std::memory_order_release);
            if (!s_connected.load(std::memory_order_acquire)) start_advertising();
            return 0;

        default:
            return 0;
    }
}

void host_task(void *) {
    ESP_LOGI(kTag, "NimBLE host started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void host_on_sync() {
    ESP_LOGI(kTag, "NimBLE host synchronized");
    if (ble_hs_id_set_rnd(s_random_addr) != 0 ||
        ble_hs_id_infer_auto(0, &s_own_addr_type) != 0) {
        ESP_LOGE(kTag, "Unable to configure random BLE address");
        s_state.store(BleState::Error, std::memory_order_release);
        return;
    }
    if (start_advertising() != ESP_OK)
        s_state.store(BleState::Error, std::memory_order_release);
}

esp_err_t init_ble() {
    s_state.store(BleState::Starting, std::memory_order_release);
    s_connected.store(false, std::memory_order_release);
    s_advertising.store(false, std::memory_order_release);
    s_stopping.store(false, std::memory_order_release);
    s_keyboard_subscribed = false;
    s_consumer_subscribed = false;
    s_host_started = false;

    uint8_t mac[6]{};
    if (esp_read_mac(mac, ESP_MAC_BT) != ESP_OK) {
        ESP_LOGE(kTag, "Unable to read Bluetooth MAC");
        s_state.store(BleState::Error, std::memory_order_release);
        return ESP_FAIL;
    }
    std::snprintf(s_device_name, sizeof(s_device_name), "Passport R2 %02X%02X",
                  mac[4], mac[5]);
    std::memcpy(s_random_addr, mac, sizeof(s_random_addr));
    s_random_addr[5] |= 0xC0;
    s_passkey.store(100000u + esp_random() % 900000u, std::memory_order_release);

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(kTag, "nimble_port_init failed: %s", esp_err_to_name(ret));
        s_state.store(BleState::Error, std::memory_order_release);
        return ret;
    }
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    // The target phone cancels passkey and Secure Connections pairing almost
    // immediately with BLE_SM_ERR_CONFIRM_MISMATCH. HID peripherals commonly use
    // Just Works bonding, which still creates an encrypted persistent link.
    ble_hs_cfg.sm_sc = 0;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sync_cb = host_on_sync;
    ble_store_config_init();
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(s_hid_services);
    if (rc != 0) {
        ESP_LOGE(kTag, "HID GATT config count failed: %d", rc);
        s_state.store(BleState::Error, std::memory_order_release);
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(s_hid_services);
    if (rc != 0) {
        ESP_LOGE(kTag, "HID GATT service registration failed: %d", rc);
        s_state.store(BleState::Error, std::memory_order_release);
        return ESP_FAIL;
    }
    ble_svc_gap_device_name_set(s_device_name);
    adv_init();
    nimble_port_freertos_init(host_task);
    s_host_started = true;
    return ESP_OK;
}

void deinit_ble() {
    ESP_LOGI(kTag, "Stopping BLE HID");
    s_stopping.store(true, std::memory_order_release);
    s_connected.store(false, std::memory_order_release);
    s_advertising.store(false, std::memory_order_release);
    s_keyboard_subscribed = false;
    s_consumer_subscribed = false;
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    }
    if (s_host_started) {
        ESP_LOGI(kTag, "Stopping NimBLE host");
        nimble_port_stop();
        s_host_started = false;
        ESP_LOGI(kTag, "NimBLE host stopped");
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    // ESP-IDF 5.5.3's dynamic NimBLE teardown corrupts the host GATT state on
    // this ESP32-C3 + Wi-Fi build. A controlled reset is the only clean way to
    // return the controller and host memory to the small internal heap.
    ESP_LOGI(kTag, "Restarting to release BLE memory");
    fflush(stdout);
    esp_restart();
}

bool wait_action_gap() {
    const int64_t now = esp_timer_get_time();
    const int64_t elapsed_ms = s_last_action_us ? (now - s_last_action_us) / 1000 : kActionGapMs;
    if (elapsed_ms < kActionGapMs) {
        vTaskDelay(pdMS_TO_TICKS(static_cast<uint32_t>(kActionGapMs - elapsed_ms)));
    }
    return s_connected.load(std::memory_order_acquire);
}

int notify_report(uint16_t handle, const void *data, size_t length,
                  bool subscribed) {
    if (!subscribed || s_conn_handle == BLE_HS_CONN_HANDLE_NONE) return 0;
    os_mbuf *om = ble_hs_mbuf_from_flat(data, static_cast<uint16_t>(length));
    if (!om) return BLE_HS_ENOMEM;
    return ble_gatts_notify_custom(s_conn_handle, handle, om);
}

void send_action(Action action) {
    if (action == Action::None || !wait_action_gap()) return;

    int result;
    const Mode mode = s_mode.load(std::memory_order_acquire);
    if (mode == Mode::Media) {
        const ConsumerReport pressed = consumer_report(action);
        const ConsumerReport released{};
        std::memcpy(s_consumer_value, &pressed, sizeof(pressed));
        result = notify_report(s_consumer_report_handle, s_consumer_value,
                               sizeof(s_consumer_value), s_consumer_subscribed);
        if (result == 0) vTaskDelay(pdMS_TO_TICKS(kKeyHoldMs));
        std::memcpy(s_consumer_value, &released, sizeof(released));
        notify_report(s_consumer_report_handle, s_consumer_value,
                      sizeof(s_consumer_value), s_consumer_subscribed);
    } else {
        const KeyboardReport pressed = keyboard_report(action);
        const KeyboardReport released{};
        std::memcpy(s_keyboard_value, &pressed, sizeof(pressed));
        result = notify_report(s_keyboard_report_handle, s_keyboard_value,
                               sizeof(s_keyboard_value), s_keyboard_subscribed);
        if (result == 0) vTaskDelay(pdMS_TO_TICKS(kKeyHoldMs));
        std::memcpy(s_keyboard_value, &released, sizeof(released));
        notify_report(s_keyboard_report_handle, s_keyboard_value,
                      sizeof(s_keyboard_value), s_keyboard_subscribed);
    }
    s_last_action_us = esp_timer_get_time();
    if (result != 0) ESP_LOGW(kTag, "HID notify failed: %d", result);
}

void worker(void *) {
    bool ready = init_ble() == ESP_OK;
    while (true) {
        Command command{};
        if (xQueueReceive(s_queue, &command, portMAX_DELAY) != pdTRUE) continue;
        if (command.type == CommandType::Quit) break;
        if (command.type == CommandType::Retry) {
            if (!ready) {
                deinit_ble();
                ready = init_ble() == ESP_OK;
            }
            continue;
        }
        if (ready && command.type == CommandType::Action) send_action(command.action);
    }
    deinit_ble();
    s_task = nullptr;
    xSemaphoreGive(s_done);
}

bool enqueue(Command command) {
    return s_queue && xQueueSend(s_queue, &command, pdMS_TO_TICKS(20)) == pdTRUE;
}

}  // namespace

void ble_start(Mode mode) {
    s_mode.store(mode, std::memory_order_release);
    if (s_task) return;
    if (!s_queue)
        s_queue = xQueueCreateStatic(kQueueDepth, sizeof(Command), s_queue_storage,
                                     &s_queue_buffer);
    if (!s_done) s_done = xSemaphoreCreateBinary();
    if (!s_queue || !s_done) {
        s_state.store(BleState::Error, std::memory_order_release);
        return;
    }
    s_state.store(BleState::Starting, std::memory_order_release);
    s_task = xTaskCreateStatic(worker, "remote_hid", kWorkerStackBytes, nullptr,
                               kWorkerPriority, s_task_stack, &s_task_buffer);
    if (!s_task) s_state.store(BleState::Error, std::memory_order_release);
}

void ble_retry() {
    enqueue({CommandType::Retry, Action::None});
}

void ble_stop() {
    if (!s_task) {
        deinit_ble();
        s_state.store(BleState::Off, std::memory_order_release);
        return;
    }
    if (!enqueue({CommandType::Quit, Action::None}))
        ESP_LOGW(kTag, "Unable to queue HID worker shutdown");
    xSemaphoreTake(s_done, pdMS_TO_TICKS(5000));
    s_task = nullptr;
}

void ble_set_mode(Mode mode) {
    s_mode.store(mode, std::memory_order_release);
}

bool ble_send(Action action) {
    if (!s_connected.load(std::memory_order_acquire) || action == Action::None)
        return false;
    return enqueue({CommandType::Action, action});
}

BleState ble_state() {
    return s_state.load(std::memory_order_acquire);
}

Mode ble_mode() {
    return s_mode.load(std::memory_order_acquire);
}

bool ble_connected() {
    return s_connected.load(std::memory_order_acquire);
}

uint32_t ble_passkey() {
    return s_passkey.load(std::memory_order_acquire);
}

const char *ble_device_name() {
    return s_device_name;
}

}  // namespace remote
