#include "apps/remote/remote_ui.h"

#include "app_fonts.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "launcher/app_registry.h"
#include "ui_pixel.h"

#include <cstdio>

#include "esp_timer.h"
#include "lvgl.h"

namespace remote {
namespace {

constexpr const char *kToolLabels[] = {"视频", "演示", "相机", "音乐"};
constexpr size_t kToolCount = sizeof(kToolLabels) / sizeof(kToolLabels[0]);
static_assert(kToolCount == 4, "BLE toolbox must expose four tools");

lv_obj_t *s_screen;
lv_obj_t *s_status_dot;
lv_obj_t *s_state;
lv_obj_t *s_device;
lv_obj_t *s_tool_cards[kToolCount];
lv_obj_t *s_tool_labels[kToolCount];
lv_obj_t *s_helper;
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
uint32_t s_camera_delay_seconds = 3;
int64_t s_camera_deadline_us;
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

size_t tool_index(Mode mode) {
    return static_cast<size_t>(mode);
}

uint32_t countdown_remaining() {
    if (!s_camera_deadline_us) return 0;
    const int64_t remaining = s_camera_deadline_us - esp_timer_get_time();
    if (remaining <= 0) return 0;
    return static_cast<uint32_t>((remaining + 999999) / 1000000);
}

void refresh_locked() {
    if (!s_screen) return;
    const BleState state = ble_state();
    const Mode mode = ble_mode();
    ui_pixel_label_set_text(s_state, state_text(state));
    ui_pixel_label_set_text(s_device, "Passport R2");

    const uint32_t status_color = state == BleState::Connected ? UI_SUCCESS
        : state == BleState::Error ? UI_DANGER
        : state == BleState::Pairing ? UI_WARNING
        : UI_ACCENT;
    ui_pixel_bg_color(s_status_dot, status_color);
    ui_pixel_text_color(s_state, state == BleState::Error ? UI_DANGER : UI_TEXT);

    for (size_t i = 0; i < kToolCount; ++i) {
        const bool selected = i == tool_index(mode);
        ui_pixel_set_selected(s_tool_cards[i], selected, true);
        ui_pixel_text_color(s_tool_labels[i],
                            selected ? UI_ACCENT : UI_TEXT_DIM);
    }

    char mode_summary[32];
    char ok_summary[32];
    char helper[32];
    std::snprintf(mode_summary, sizeof(mode_summary), "工具 %s", mode_name(mode));

    const char *up = "上一条";
    const char *down = "下一条";
    const char *ok = "播放 / 暂停";
    switch (mode) {
        case Mode::Video:
            std::snprintf(helper, sizeof(helper), "媒体键");
            break;
        case Mode::Presentation:
            up = "上一页";
            down = "下一页";
            ok = "开始演示";
            std::snprintf(helper, sizeof(helper), "Page / F5");
            break;
        case Mode::Camera: {
            up = "延时 -";
            down = "延时 +";
            ok = "拍照";
            const uint32_t remaining = countdown_remaining();
            if (remaining) {
                std::snprintf(helper, sizeof(helper), "倒计时中");
                std::snprintf(ok_summary, sizeof(ok_summary), "%u秒后拍照",
                              static_cast<unsigned>(remaining));
                ok = ok_summary;
            } else {
                std::snprintf(helper, sizeof(helper), "延时 %u秒",
                              static_cast<unsigned>(s_camera_delay_seconds));
            }
            break;
        }
        case Mode::Music:
            up = "上一首";
            down = "下一首";
            std::snprintf(helper, sizeof(helper), "媒体键");
            break;
    }

    ui_pixel_label_set_text(s_helper, helper);
    ui_pixel_label_set_text(s_mode, mode_summary);
    ui_pixel_label_set_text(s_last_action, s_last_action_text);
    ui_pixel_label_set_text(s_up_action, up);
    ui_pixel_label_set_text(s_down_action, down);
    ui_pixel_label_set_text(s_ok_action, ok);
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
    lv_obj_set_style_bg_color(
        target, lv_color_hex(pressed ? UI_ACCENT : UI_SURFACE), 0);
    lv_obj_set_style_border_color(
        target, lv_color_hex(pressed ? UI_ACCENT : UI_BORDER), 0);
    lv_obj_set_style_border_width(target, pressed ? 2 : 1, 0);
    if (is_up) lv_obj_set_y(s_up_icon, pressed ? 0 : 2);
    if (is_down) lv_obj_set_y(s_down_icon, pressed ? 4 : 2);
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

const char *action_text_for(Mode mode, uint8_t button, bool long_press) {
    if (long_press) return "最近 换工具";
    if (mode == Mode::Presentation) {
        if (button == BSP_BTN_UP) return "最近 上一页";
        if (button == BSP_BTN_DOWN) return "最近 下一页";
        return "最近 开始演示";
    }
    if (mode == Mode::Camera) {
        if (button == BSP_BTN_UP) return "最近 延时 -";
        if (button == BSP_BTN_DOWN) return "最近 延时 +";
        return "最近 拍照";
    }
    if (mode == Mode::Music) {
        if (button == BSP_BTN_UP) return "最近 上一首";
        if (button == BSP_BTN_DOWN) return "最近 下一首";
        return "最近 播放暂停";
    }
    if (button == BSP_BTN_UP) return "最近 上一条";
    if (button == BSP_BTN_DOWN) return "最近 下一条";
    return "最近 播放暂停";
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
    lv_obj_t *title = ui_pixel_label(top, "BLE 工具箱", &buddy_font_16, UI_TEXT);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 42, 0);
    s_battery = ui_pixel_battery_create(top, UI_STATUS_BATTERY_X, 6, UI_TEXT,
                                        &lv_font_montserrat_14);

    lv_obj_t *status = panel(s_screen, 12, 32, 216, 40, UI_SURFACE, UI_BORDER);
    s_status_dot = panel(status, 9, 11, 7, 7, UI_ACCENT, UI_ACCENT);
    lv_obj_set_style_radius(s_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_status_dot, 0, 0);
    lv_obj_t *ble = ui_pixel_label(status, "BLE HID",
                                    &lv_font_montserrat_14, UI_TEXT_DIM);
    label_at(status, ble, 22, 3, 72);
    s_device = ui_pixel_label(status, "", &lv_font_montserrat_14, UI_TEXT_DIM);
    label_at(status, s_device, 90, 3, 120);
    lv_obj_set_style_text_align(s_device, LV_TEXT_ALIGN_RIGHT, 0);
    s_state = ui_pixel_label(status, "", &buddy_font_16, UI_TEXT);
    label_at(status, s_state, 22, 18, 188);

    for (size_t i = 0; i < kToolCount; ++i) {
        s_tool_cards[i] = panel(
            s_screen, 12 + static_cast<int>(i) * 54, 79, 52, 38,
            UI_SURFACE, UI_BORDER);
        s_tool_labels[i] = ui_pixel_label(
            s_tool_cards[i], kToolLabels[i], &buddy_font_16, UI_TEXT_DIM);
        lv_obj_center(s_tool_labels[i]);
    }

    s_helper = ui_pixel_label(s_screen, "", &buddy_font_16, UI_TEXT_DIM);
    label_at(s_screen, s_helper, 12, 121, 216);
    lv_obj_set_style_text_align(s_helper, LV_TEXT_ALIGN_RIGHT, 0);

    s_up_panel = panel(s_screen, 12, 138, 104, 56, UI_SURFACE, UI_BORDER);
    s_up_icon = ui_pixel_label(s_up_panel, "UP",
                               &lv_font_montserrat_20, UI_ACCENT);
    lv_obj_align(s_up_icon, LV_ALIGN_TOP_MID, 0, 2);
    s_up_action = ui_pixel_label(s_up_panel, "", &buddy_font_16, UI_TEXT);
    label_at(s_up_panel, s_up_action, 5, 31, 94);
    lv_obj_set_style_text_align(s_up_action, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_up_action, LV_LABEL_LONG_MODE_DOTS);

    s_down_panel = panel(s_screen, 124, 138, 104, 56, UI_SURFACE, UI_BORDER);
    s_down_icon = ui_pixel_label(s_down_panel, "DN",
                                 &lv_font_montserrat_20, UI_ACCENT);
    lv_obj_align(s_down_icon, LV_ALIGN_TOP_MID, 0, 2);
    s_down_action = ui_pixel_label(s_down_panel, "", &buddy_font_16, UI_TEXT);
    label_at(s_down_panel, s_down_action, 5, 31, 94);
    lv_obj_set_style_text_align(s_down_action, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_down_action, LV_LABEL_LONG_MODE_DOTS);

    s_ok_panel = panel(s_screen, 12, 201, 216, 36, UI_SURFACE, UI_BORDER);
    s_ok_badge = panel(s_ok_panel, 9, 4, 28, 28, UI_SURFACE, UI_ACCENT);
    lv_obj_set_style_radius(s_ok_badge, LV_RADIUS_CIRCLE, 0);
    s_ok_label = ui_pixel_label(s_ok_badge, "OK",
                                &lv_font_montserrat_14, UI_ACCENT);
    lv_obj_center(s_ok_label);
    s_ok_action = ui_pixel_label(s_ok_panel, "", &buddy_font_16, UI_TEXT);
    label_at(s_ok_panel, s_ok_action, 46, 8, 160);
    lv_obj_set_style_text_align(s_ok_action, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_ok_action, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t *mode_panel = panel(s_screen, 12, 243, 104, 28,
                                 UI_SURFACE, UI_BORDER);
    s_mode = ui_pixel_label(mode_panel, "", &buddy_font_16, UI_ACCENT);
    lv_obj_center(s_mode);
    lv_obj_t *recent_panel = panel(s_screen, 124, 243, 104, 28,
                                   UI_SURFACE, UI_BORDER);
    s_last_action = ui_pixel_label(recent_panel, "", &buddy_font_16, UI_TEXT);
    lv_obj_center(s_last_action);

    ui_pixel_footer(s_screen, "上下操作 · 确定执行",
                    "长按上/下换工具", &buddy_font_16);
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
    s_status_dot = s_state = s_device = s_helper = s_mode = nullptr;
    s_last_action = s_ok_action = s_ok_badge = nullptr;
    s_up_action = s_down_action = nullptr;
    s_up_panel = s_down_panel = s_ok_panel = nullptr;
    s_up_icon = s_down_icon = s_ok_label = nullptr;
    for (size_t i = 0; i < kToolCount; ++i) {
        s_tool_cards[i] = nullptr;
        s_tool_labels[i] = nullptr;
    }
    s_camera_deadline_us = 0;
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
        s_last_action_text = action_text_for(
            ble_mode(), button, event == BTN_EVT_LONG);
        ui_pixel_label_set_text(s_last_action, s_last_action_text);
        if (s_press_timer) lv_timer_delete(s_press_timer);
        s_press_timer = lv_timer_create(restore_pressed, 140, nullptr);
        if (s_press_timer) lv_timer_set_repeat_count(s_press_timer, 1);
    }
    bsp_lvgl_unlock();
}

void ui_sync(BleState, Mode, uint32_t camera_delay_seconds) {
    if (!bsp_lvgl_lock(50)) return;
    s_camera_delay_seconds = camera_delay_seconds;
    refresh_locked();
    bsp_lvgl_unlock();
}

void ui_camera_countdown_start(uint32_t seconds) {
    if (!bsp_lvgl_lock(50)) return;
    s_camera_deadline_us = esp_timer_get_time() +
        static_cast<int64_t>(seconds) * 1000000;
    refresh_locked();
    bsp_lvgl_unlock();
}

void ui_camera_countdown_cancel() {
    if (!bsp_lvgl_lock(50)) return;
    s_camera_deadline_us = 0;
    refresh_locked();
    bsp_lvgl_unlock();
}

void ui_camera_countdown_end() {
    ui_camera_countdown_cancel();
}

}  // namespace remote
