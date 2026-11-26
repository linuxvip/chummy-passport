#include "apps/remote/app_remote.h"

#include "apps/remote/ble_hid_remote.h"
#include "apps/remote/remote_controls.h"
#include "apps/remote/remote_ui.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

#include <cstddef>
#include <cstdint>

namespace remote {
namespace {

constexpr const char *kTag = "remote";
constexpr const char *kNvsNamespace = "remote";
constexpr const char *kModeKey = "tool_v1";
constexpr const char *kDelayKey = "camera_delay";
constexpr uint8_t kCameraDelays[] = {0, 3, 5, 10};

Mode s_mode = Mode::Video;
uint8_t s_camera_delay_index = 1;
esp_timer_handle_t s_shutter_timer = nullptr;

void load_mode() {
    nvs_handle_t handle;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) return;
    uint8_t value = static_cast<uint8_t>(Mode::Video);
    if (nvs_get_u8(handle, kModeKey, &value) == ESP_OK && is_mode(value))
        s_mode = static_cast<Mode>(value);
    value = 1;
    if (nvs_get_u8(handle, kDelayKey, &value) == ESP_OK && value < 4)
        s_camera_delay_index = value;
    nvs_close(handle);
}

void save_mode_and_delay() {
    nvs_handle_t handle;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) return;
    esp_err_t result = nvs_set_u8(handle, kModeKey, static_cast<uint8_t>(s_mode));
    if (result == ESP_OK)
        result = nvs_set_u8(handle, kDelayKey, s_camera_delay_index);
    if (result == ESP_OK) nvs_commit(handle);
    nvs_close(handle);
    if (result != ESP_OK)
        ESP_LOGW(kTag, "Failed to save tool settings: %s", esp_err_to_name(result));
}

void shutter_timer_callback(void *) {
    ui_camera_countdown_end();
    ble_send(Action::Toggle);
}

void cancel_shutter() {
    if (!s_shutter_timer) return;
    esp_timer_stop(s_shutter_timer);
    ui_camera_countdown_cancel();
}

void trigger_shutter() {
    if (!ble_connected()) return;
    const uint32_t delay = kCameraDelays[s_camera_delay_index];
    if (delay == 0) {
        ble_send(Action::Toggle);
        return;
    }
    if (!s_shutter_timer) {
        esp_timer_create_args_t args{};
        args.callback = shutter_timer_callback;
        args.name = "camera_shutter";
        args.dispatch_method = ESP_TIMER_TASK;
        args.skip_unhandled_events = true;
        if (esp_timer_create(&args, &s_shutter_timer) != ESP_OK) return;
    }
    if (esp_timer_is_active(s_shutter_timer)) return;
    ui_camera_countdown_start(delay);
    esp_timer_start_once(s_shutter_timer, static_cast<uint64_t>(delay) * 1000000ULL);
}

void start() {
    load_mode();
    ui_show();
    ble_start(s_mode);
    ui_sync(ble_state(), s_mode, kCameraDelays[s_camera_delay_index]);
}

void stop() {
    cancel_shutter();
    if (s_shutter_timer) {
        esp_timer_delete(s_shutter_timer);
        s_shutter_timer = nullptr;
    }
    ble_stop();
    ui_hide();
}

void on_key(uint8_t button, app_btn_event_t event, uint16_t) {
    ui_key_event(button, event);
    if (event == BTN_EVT_LONG && (button == BSP_BTN_UP || button == BSP_BTN_DOWN)) {
        cancel_shutter();
        s_mode = cycle_mode(s_mode, button == BSP_BTN_UP ? -1 : 1);
        save_mode_and_delay();
        ble_set_mode(s_mode);
        ui_sync(ble_state(), s_mode, kCameraDelays[s_camera_delay_index]);
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
    if (s_mode == Mode::Camera) {
        if (action == Action::Toggle) {
            trigger_shutter();
        } else {
            s_camera_delay_index = cycle_camera_delay(
                s_camera_delay_index, action == Action::Previous ? -1 : 1);
            save_mode_and_delay();
            ui_sync(ble_state(), s_mode, kCameraDelays[s_camera_delay_index]);
        }
        return;
    }
    if (!ble_send(action))
        ESP_LOGD(kTag, "HID input ignored: state=%d", static_cast<int>(ble_state()));
}

}  // namespace
}  // namespace remote

extern const passport_app_t g_remote_app;
const passport_app_t g_remote_app = {
    .id = APP_ID_REMOTE,
    .name = "BLE 工具箱",
    .en_name = "BLE TOOLBOX",
    .desc = "BLE HID 短视频、演示、相机与音乐",
    .tag = "BLE",
    .theme_color = 0x3D8C7C,
    .init = nullptr,
    .start = remote::start,
    .stop = remote::stop,
    .on_key = remote::on_key,
    .prepare_stop = nullptr,
    .on_back = nullptr,
};
