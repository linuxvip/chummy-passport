#include "apps/remote/app_remote.h"

#include "apps/remote/ble_hid_remote.h"
#include "apps/remote/remote_controls.h"
#include "apps/remote/remote_ui.h"

#include "esp_log.h"
#include "nvs.h"

#include <cstdint>

namespace remote {
namespace {

constexpr const char *kTag = "remote";
constexpr const char *kNvsNamespace = "remote";
constexpr const char *kModeKey = "hid_mode_v2";

Mode s_mode = Mode::Media;

void load_mode() {
    nvs_handle_t handle;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) return;
    uint8_t value = static_cast<uint8_t>(Mode::Media);
    if (nvs_get_u8(handle, kModeKey, &value) == ESP_OK && is_mode(value))
        s_mode = static_cast<Mode>(value);
    nvs_close(handle);
}

void save_mode() {
    nvs_handle_t handle;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) return;
    const esp_err_t result = nvs_set_u8(handle, kModeKey, static_cast<uint8_t>(s_mode));
    if (result == ESP_OK) nvs_commit(handle);
    nvs_close(handle);
    if (result != ESP_OK)
        ESP_LOGW(kTag, "Failed to save HID mode: %s", esp_err_to_name(result));
}

void start() {
    load_mode();
    ui_show();
    ble_start(s_mode);
}

void stop() {
    ble_stop();
    ui_hide();
}

void on_key(uint8_t button, app_btn_event_t event, uint16_t) {
    ui_key_event(button, event);
    if (event == BTN_EVT_LONG && (button == BSP_BTN_UP || button == BSP_BTN_DOWN)) {
        s_mode = cycle_mode(s_mode, button == BSP_BTN_UP ? -1 : 1);
        save_mode();
        ble_set_mode(s_mode);
        ui_sync(ble_state(), s_mode, ble_passkey());
        ESP_LOGI(kTag, "HID mode: %s", mode_name(s_mode));
        return;
    }
    if (event != BTN_EVT_CLICK) return;

    if (button == BSP_BTN_OK && ble_state() == BleState::Error) {
        ble_retry();
        return;
    }
    const Action action = action_for(button, event);
    if (action == Action::None) return;
    if (!ble_send(action))
        ESP_LOGD(kTag, "HID input ignored: state=%d", static_cast<int>(ble_state()));
}

}  // namespace
}  // namespace remote

extern const passport_app_t g_remote_app;
const passport_app_t g_remote_app = {
    .id = APP_ID_REMOTE,
    .name = "手机遥控",
    .en_name = "PHONE REMOTE",
    .desc = "BLE HID 翻页与播放",
    .tag = "BLE",
    .theme_color = 0x3D8C7C,
    .init = nullptr,
    .start = remote::start,
    .stop = remote::stop,
    .on_key = remote::on_key,
    .prepare_stop = nullptr,
    .on_back = nullptr,
};
