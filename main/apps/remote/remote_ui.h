#pragma once

#include "apps/remote/ble_hid_remote.h"

namespace remote {

void ui_show();
void ui_hide();
void ui_key_event(uint8_t button, app_btn_event_t event);
void ui_sync(BleState state, Mode mode, uint32_t camera_delay_seconds);
void ui_camera_countdown_start(uint32_t seconds);
void ui_camera_countdown_cancel();
void ui_camera_countdown_end();

}  // namespace remote
