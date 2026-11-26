"""Guard the BLE HID remote contract and launcher entry."""

from pathlib import Path
import re


root = Path(__file__).resolve().parents[1]
ble = (root / "main/apps/remote/ble_hid_remote.cc").read_text(encoding="utf8")
reports = (root / "main/apps/remote/hid_reports.h").read_text(encoding="utf8")
app = (root / "main/apps/remote/app_remote.cc").read_text(encoding="utf8")
controls = (root / "main/apps/remote/remote_controls.h").read_text(encoding="utf8")
ui = (root / "main/apps/remote/remote_ui.cc").read_text(encoding="utf8")
settings = (root / "main/launcher/settings.cc").read_text(encoding="utf8")
registry = (root / "main/launcher/app_registry.cc").read_text(encoding="utf8")
app_header = (root / "main/apps/app_base.h").read_text(encoding="utf8")
defaults = (root / "sdkconfig.defaults").read_text(encoding="utf8")
cmake = (root / "main/CMakeLists.txt").read_text(encoding="utf8")

assert "CONFIG_BT_ENABLED=y" in defaults
assert "CONFIG_BT_NIMBLE_ENABLED=y" in defaults
assert "CONFIG_BT_NIMBLE_HID_SERVICE=y" not in defaults
assert "CONFIG_BT_NIMBLE_NVS_PERSIST=y" in defaults
assert "CONFIG_BT_NIMBLE_SM_LVL=2" in defaults
assert "CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1" in defaults
assert "CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT=n" in defaults
assert "CONFIG_BT_NIMBLE_PROX_SERVICE=n" in defaults
assert "CONFIG_BT_NIMBLE_HR_SERVICE=n" in defaults
assert "CONFIG_BT_CTRL_BLE_MAX_ACT=2" in defaults
assert "esp_hid" not in cmake and "bt" in cmake
assert '"apps/remote/ble_hid_remote.cc"' in cmake

assert "ble_gatts_add_svcs(s_hid_services)" in ble
assert "ble_gatts_notify_custom" in ble
assert "s_keyboard_report_handle" in ble
assert "s_consumer_report_handle" in ble
assert "ble_store_config_init" in ble
assert "ble_gap_security_initiate" not in ble
assert "BLE_SM_IO_CAP_NO_IO" in ble
assert "ble_sm_inject_io" in ble
assert "ble_hs_cfg.sm_mitm = 0" in ble
assert "ble_hs_cfg.sm_sc = 0" in ble
assert "ble_gap_adv_start" in ble
assert "ble_hs_id_set_rnd(s_random_addr)" in ble
assert "ble_hs_id_infer_auto(0, &s_own_addr_type)" in ble
assert '"Passport R2 %02X%02X"' in ble
assert "ble_hs_cfg.sync_cb = host_on_sync" in ble
assert "if (start_advertising() != ESP_OK)" in ble
assert "BLE_GAP_EVENT_DISCONNECT" in ble
assert "BLE_GAP_EVENT_ENC_CHANGE" in ble
assert "nimble_port_stop" in ble
assert "esp_restart()" in ble
assert "nimble_port_deinit" not in ble
assert "kKeyHoldMs = 20" in ble
assert "kActionGapMs = 100" in ble

assert reports.count("0x85, kKeyboardReportId") == 1
assert reports.count("0x85, kConsumerReportId") == 1
assert "kMouseReportMap" not in reports
assert "0x95, 0x06" in reports
assert "0x05, 0x0C" in reports
assert "kConsumerPlayPause" in controls
assert "sizeof(pressed)" in ble
assert "sizeof(released)" in ble

assert "BTN_EVT_LONG" in app
assert "Media" in controls and "Arrows" in controls and "Mouse" not in controls
assert "Mode::Media" in app
assert "cycle_mode(s_mode" in app
assert "ble_retry()" in app
assert "nvs_set_u8(handle, kModeKey" in app
assert "nvs_get_u8(handle, kModeKey" in app

assert '"手机遥控"' in ui
assert '"等待手机连接"' in ui
assert '"手机确认连接"' in ui
assert '"上一条"' in ui and '"下一条"' in ui
assert '"长按上/下换模式"' in ui
assert '"长按确定返回设置"' not in ui
assert "app_registry_keep_awake()" in ui
assert "void ui_key_event" in ui and "ui_key_event(button, event)" in app
assert "lv_timer_set_repeat_count(s_press_timer, 1)" in ui
assert "s_up_panel = panel(s_screen, 12, 116, 104, 70, UI_SURFACE, UI_BORDER)" in ui
assert "s_down_panel = panel(s_screen, 124, 116, 104, 70, UI_SURFACE, UI_BORDER)" in ui
assert '"最近 播放暂停"' in ui
for x, y, width in re.findall(
    r"panel\(s_screen, (\d+), (\d+), (\d+), \d+", ui
):
    assert int(x) >= 0 and int(y) >= 0 and int(x) + int(width) <= 240

assert "APP_ID_REMOTE" in app_header
assert "APP_ID_REMOTE" not in settings
assert '"手机遥控"' not in settings
assert registry.count("&g_remote_app") == 1, "remote must be a launcher app"

print("Remote BLE HID: direct GATT service, security, lifecycle, launcher entry and compact UI PASS")
