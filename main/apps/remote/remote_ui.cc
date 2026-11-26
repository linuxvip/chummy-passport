#include "apps/remote/remote_ui.h"

#include "app_fonts.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "launcher/app_registry.h"
#include "ui_pixel.h"

#include <cstdio>

#include "lvgl.h"

namespace remote {
namespace {

lv_obj_t *s_screen;
lv_obj_t *s_status_badge;
lv_obj_t *s_status_glyph;
lv_obj_t *s_state;
lv_obj_t *s_detail;
lv_obj_t *s_mode;
lv_obj_t *s_last_action;
lv_obj_t *s_up_action;
lv_obj_t *s_down_action;
lv_obj_t *s_up_panel;
lv_obj_t *s_down_panel;
lv_obj_t *s_ok_panel;
lv_obj_t *s_ok_badge;
lv_obj_t *s_up_icon;
lv_obj_t *s_down_icon;
lv_obj_t *s_ok_label;
lv_obj_t *s_ok_action;
lv_timer_t *s_timer;
lv_timer_t *s_press_timer;
uint8_t s_pressed_button;
ui_pixel_battery_t s_battery{};
const char *s_last_action_text = "最近 等待";

lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h,
                uint32_t color, uint32_t border) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 7, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(border), 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

void label_at(lv_obj_t *parent, lv_obj_t *label, int x, int y, int w) {
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, w);
}

const char *state_text(BleState state) {
    switch (state) {
        case BleState::Off: return "蓝牙已关闭";
        case BleState::Starting: return "正在启动蓝牙";
        case BleState::Advertising: return "等待手机连接";
        case BleState::Pairing: return "请在手机上确认";
        case BleState::Connected: return "已连接";
        case BleState::Error: return "蓝牙不可用";
    }
    return "未知状态";
}

void refresh_locked() {
    if (!s_screen) return;
    const BleState state = ble_state();
    const Mode mode = ble_mode();
    char detail[64];
    switch (state) {
        case BleState::Starting:
            std::snprintf(detail, sizeof(detail), "首次启动约需 1 秒");
            break;
        case BleState::Advertising:
            std::snprintf(detail, sizeof(detail), "等待手机搜索 Passport R2");
            break;
        case BleState::Pairing:
            std::snprintf(detail, sizeof(detail), "手机确认连接");
            break;
        case BleState::Connected:
            std::snprintf(detail, sizeof(detail), "%s · HID 在线",
                          ble_device_name());
            break;
        case BleState::Error:
            std::snprintf(detail, sizeof(detail), "确定重试");
            break;
        case BleState::Off:
            std::snprintf(detail, sizeof(detail), "长按确定返回");
            break;
    }

    char mode_summary[32];
    std::snprintf(mode_summary, sizeof(mode_summary), "模式 %s",
                  mode == Mode::Media ? "媒体" : "方向键");
    ui_pixel_label_set_text(s_state, state_text(state));
    ui_pixel_label_set_text(s_detail, detail);
    ui_pixel_label_set_text(s_mode, mode_summary);
    ui_pixel_label_set_text(s_last_action, s_last_action_text);
    ui_pixel_label_set_text(s_up_action, "上一条");
    ui_pixel_label_set_text(s_down_action, "下一条");

    const uint32_t status_color = state == BleState::Connected ? UI_SUCCESS
        : state == BleState::Error ? UI_DANGER
        : state == BleState::Pairing ? UI_WARNING
        : UI_ACCENT;
    ui_pixel_bg_color(s_status_badge, status_color);
    ui_pixel_label_set_text(
        s_status_glyph,
        state == BleState::Connected ? "OK"
        : state == BleState::Error ? "!"
        : state == BleState::Pairing ? ".."
        : "...");
    ui_pixel_text_color(s_state, state == BleState::Error ? UI_DANGER : UI_TEXT);
    ui_pixel_battery_set(&s_battery, bsp_battery_soc());
}

void timer_callback(lv_timer_t *) {
    app_registry_keep_awake();
    refresh_locked();
}

void set_pressed(uint8_t button, bool pressed) {
    lv_obj_t *target = button == BSP_BTN_UP ? s_up_panel
        : button == BSP_BTN_DOWN ? s_down_panel
        : button == BSP_BTN_OK ? s_ok_panel : nullptr;
    if (!target) return;
    const bool is_up = button == BSP_BTN_UP;
    const bool is_down = button == BSP_BTN_DOWN;
    const uint32_t base = UI_SURFACE;
    lv_obj_set_style_bg_color(
        target, lv_color_hex(pressed ? UI_ACCENT : base), 0);
    lv_obj_set_style_border_color(
        target, lv_color_hex(pressed ? UI_ACCENT : UI_BORDER), 0);
    lv_obj_set_style_border_width(target, pressed ? 2 : 1, 0);
    if (is_up) lv_obj_set_y(s_up_icon, pressed ? 1 : 5);
    if (is_down) lv_obj_set_y(s_down_icon, pressed ? 9 : 5);
    if (is_up || is_down) {
        lv_obj_t *icon = is_up ? s_up_icon : s_down_icon;
        lv_obj_t *text = is_up ? s_up_action : s_down_action;
        ui_pixel_text_color(icon, pressed ? UI_ON_ACCENT : UI_ACCENT);
        ui_pixel_text_color(text, pressed ? UI_ON_ACCENT : UI_TEXT);
    }
    if (button == BSP_BTN_OK)
        ui_pixel_text_color(s_ok_action, pressed ? UI_ON_ACCENT : UI_TEXT);
}

void restore_pressed(lv_timer_t *timer) {
    set_pressed(s_pressed_button, false);
    if (s_press_timer == timer) s_press_timer = nullptr;
}

}  // namespace

void ui_show() {
    if (!bsp_lvgl_lock(500)) return;
    if (s_screen) {
        refresh_locked();
        bsp_lvgl_unlock();
        return;
    }

    s_screen = lv_obj_create(nullptr);
    ui_pixel_background(s_screen);

    lv_obj_t *top = ui_pixel_top_bar(s_screen);
    lv_obj_t *title = ui_pixel_label(top, "手机遥控", &buddy_font_16, UI_TEXT);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 42, 0);
    s_battery = ui_pixel_battery_create(top, UI_STATUS_BATTERY_X, 6, UI_TEXT,
                                        &lv_font_montserrat_14);

    lv_obj_t *status = panel(s_screen, 12, 36, 216, 60, UI_SURFACE, UI_BORDER);
    s_status_badge = panel(status, 10, 13, 30, 30, UI_ACCENT, UI_ACCENT);
    lv_obj_set_style_radius(s_status_badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_status_badge, 0, 0);
    s_status_glyph = ui_pixel_label(s_status_badge, "ON",
                                     &lv_font_montserrat_14, UI_ON_ACCENT);
    lv_obj_center(s_status_glyph);
    lv_obj_t *ble = ui_pixel_label(status, "BLE HID", &lv_font_montserrat_14, UI_TEXT_DIM);
    label_at(status, ble, 48, 7, 80);
    s_state = ui_pixel_label(status, "", &buddy_font_16, UI_TEXT);
    label_at(status, s_state, 48, 23, 156);
    s_detail = ui_pixel_label(status, "", &buddy_font_16, UI_TEXT_DIM);
    label_at(status, s_detail, 10, 41, 196);
    lv_label_set_long_mode(s_detail, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t *control = ui_pixel_label(s_screen, "视频控制",
                                       &buddy_font_16, UI_TEXT_DIM);
    label_at(s_screen, control, 12, 99, 100);

    s_up_panel = panel(s_screen, 12, 116, 104, 70, UI_SURFACE, UI_BORDER);
    s_up_icon = ui_pixel_label(s_up_panel, "UP", &lv_font_montserrat_20, UI_ACCENT);
    lv_obj_align(s_up_icon, LV_ALIGN_TOP_MID, 0, 4);
    s_up_action = ui_pixel_label(s_up_panel, "", &buddy_font_16, UI_TEXT);
    label_at(s_up_panel, s_up_action, 5, 40, 94);
    lv_obj_set_style_text_align(s_up_action, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_up_action, LV_LABEL_LONG_MODE_DOTS);

    s_down_panel = panel(s_screen, 124, 116, 104, 70, UI_SURFACE, UI_BORDER);
    s_down_icon = ui_pixel_label(s_down_panel, "DN", &lv_font_montserrat_20, UI_ACCENT);
    lv_obj_align(s_down_icon, LV_ALIGN_TOP_MID, 0, 4);
    s_down_action = ui_pixel_label(s_down_panel, "", &buddy_font_16, UI_TEXT);
    label_at(s_down_panel, s_down_action, 5, 40, 94);
    lv_obj_set_style_text_align(s_down_action, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_down_action, LV_LABEL_LONG_MODE_DOTS);

    s_ok_panel = panel(s_screen, 12, 192, 216, 38, UI_SURFACE, UI_BORDER);
    s_ok_badge = panel(s_ok_panel, 10, 5, 28, 28, UI_SURFACE, UI_ACCENT);
    lv_obj_set_style_radius(s_ok_badge, LV_RADIUS_CIRCLE, 0);
    s_ok_label = ui_pixel_label(s_ok_badge, "OK", &lv_font_montserrat_14, UI_ACCENT);
    lv_obj_center(s_ok_label);
    s_ok_action = ui_pixel_label(s_ok_panel, "播放 / 暂停",
                                 &buddy_font_16, UI_TEXT);
    label_at(s_ok_panel, s_ok_action, 48, 9, 158);
    lv_obj_set_style_text_align(s_ok_action, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *mode_panel = panel(s_screen, 12, 236, 104, 32,
                                 UI_SURFACE, UI_BORDER);
    s_mode = ui_pixel_label(mode_panel, "", &buddy_font_16, UI_ACCENT);
    lv_obj_center(s_mode);
    lv_obj_t *recent_panel = panel(s_screen, 124, 236, 104, 32,
                                   UI_SURFACE, UI_BORDER);
    s_last_action = ui_pixel_label(recent_panel, "", &buddy_font_16, UI_TEXT);
    lv_obj_center(s_last_action);

    ui_pixel_footer(s_screen, "上/下翻页 · 确定播放",
                    "长按上/下换模式",
                    &buddy_font_16);
    lv_screen_load(s_screen);
    refresh_locked();
    s_timer = lv_timer_create(timer_callback, 250, nullptr);
    bsp_lvgl_unlock();
}

void ui_hide() {
    bsp_lvgl_lock(-1);
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = nullptr;
    }
    if (s_press_timer) {
        lv_timer_delete(s_press_timer);
        s_press_timer = nullptr;
    }
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = nullptr;
    }
    s_status_badge = s_status_glyph = s_state = s_detail = s_mode = nullptr;
    s_last_action = nullptr;
    s_ok_action = nullptr;
    s_ok_badge = nullptr;
    s_up_action = s_down_action = nullptr;
    s_up_panel = s_down_panel = s_ok_panel = nullptr;
    s_up_icon = s_down_icon = s_ok_label = nullptr;
    s_battery = {};
    bsp_lvgl_unlock();
}

void ui_key_event(uint8_t button, app_btn_event_t event) {
    if (!bsp_lvgl_lock(50)) return;
    if (event == BTN_EVT_PRESS) {
        s_pressed_button = button;
        set_pressed(button, true);
    } else if (event == BTN_EVT_RELEASE || event == BTN_EVT_LONG_UP) {
        set_pressed(button, false);
    } else if (event == BTN_EVT_CLICK || event == BTN_EVT_LONG) {
        s_pressed_button = button;
        set_pressed(button, true);
        s_last_action_text = event == BTN_EVT_LONG ? "最近 换模式"
            : button == BSP_BTN_UP ? "最近 上一条"
            : button == BSP_BTN_DOWN ? "最近 下一条"
            : "最近 播放暂停";
        ui_pixel_label_set_text(s_last_action, s_last_action_text);
        if (s_press_timer) lv_timer_delete(s_press_timer);
        s_press_timer = lv_timer_create(restore_pressed, 140, nullptr);
        if (s_press_timer) lv_timer_set_repeat_count(s_press_timer, 1);
    }
    bsp_lvgl_unlock();
}

void ui_sync(BleState, Mode, uint32_t) {
    if (!bsp_lvgl_lock(50)) return;
    refresh_locked();
    bsp_lvgl_unlock();
}

}  // namespace remote
