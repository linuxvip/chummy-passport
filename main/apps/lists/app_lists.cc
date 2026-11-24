#include "list_service.h"

#include "apps/app_base.h"
#include "launcher/app_registry.h"
#include "bsp_display.h"
#include "bsp_battery.h"
#include "app_fonts.h"
#include "ui_pixel.h"

#include <cstdio>
#include <memory>
#include <new>

namespace lists {
namespace {

struct View {
    Checklist checklist{};
    size_t selected = 0;
    uint32_t revision = UINT32_MAX;
    bool delete_selected = false;
    bool clear_completed = false;
};

View *view;
lv_obj_t *screen;
lv_obj_t *heading;
lv_obj_t *content;
lv_obj_t *footer;
lv_obj_t *footer_secondary;
lv_obj_t *rows[3];
lv_timer_t *timer;
ui_pixel_battery_t battery{};
const char *message;

lv_obj_t *label(const char *text, int x, int y, int width, int height,
                uint32_t color) {
    lv_obj_t *object = ui_pixel_label(screen, text, &buddy_font_16, color);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_label_set_long_mode(object, LV_LABEL_LONG_WRAP);
    return object;
}

void set_footer(const char *primary, const char *secondary) {
    ui_pixel_label_set_text(footer, primary);
    ui_pixel_label_set_text(footer_secondary, secondary);
}

void refresh() {
    if (!screen || !view) return;
    View &state = *view;
    const char *error = nullptr;
    const uint32_t observed_revision = revision();
    if (state.revision != observed_revision) {
        std::unique_ptr<Checklist> loaded(new (std::nothrow) Checklist);
        if (loaded && snapshot(loaded.get(), &error)) {
            state.checklist = *loaded;
            // If another edit lands during snapshot(), the next timer tick
            // observes it instead of accidentally marking stale data current.
            state.revision = observed_revision;
            message = nullptr;
        } else {
            message = loaded ? error : "内存不足，请退出后重试";
        }
    }

    ui_pixel_battery_set(&battery, bsp_battery_soc());
    for (lv_obj_t *row : rows) lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(content, LV_OBJ_FLAG_HIDDEN);

    const size_t count = state.checklist.count;
    if (state.selected >= count) state.selected = count ? count - 1 : 0;
    const size_t completed = completed_count(state.checklist);

    if (state.delete_selected) {
        if (!count) {
            state.delete_selected = false;
            refresh();
            return;
        }
        lv_obj_remove_flag(content, LV_OBJ_FLAG_HIDDEN);
        ui_pixel_label_set_text(heading, "删除事项？");
        ui_pixel_label_set_text(content, state.checklist.items[state.selected].text);
        set_footer("确定删除 · 上键取消", "长按确定返回");
        return;
    }
    if (state.clear_completed) {
        char title[48];
        std::snprintf(title, sizeof(title), "清除 %u 条已完成事项？",
                      static_cast<unsigned>(completed));
        lv_obj_remove_flag(content, LV_OBJ_FLAG_HIDDEN);
        ui_pixel_label_set_text(heading, title);
        ui_pixel_label_set_text(content, "删除后不可恢复\n\n请确认是否继续");
        set_footer("确定清除 · 上键取消", "长按确定返回");
        return;
    }

    if (!count) {
        lv_obj_remove_flag(content, LV_OBJ_FLAG_HIDDEN);
        ui_pixel_label_set_text(heading, "还没有事项");
        ui_pixel_label_set_text(
            content,
            message ? message :
            "对小智说：\n\n添加事项“检查夜间主题”\n\n也可以一次说三条");
        set_footer("确定打开小智", "长按确定返回");
        return;
    }

    char title[32];
    std::snprintf(title, sizeof(title), "已完成 %u / %u",
                  static_cast<unsigned>(completed),
                  static_cast<unsigned>(count));
    ui_pixel_label_set_text(heading, title);
    const size_t start = state.selected / 3 * 3;
    for (size_t i = 0; i < 3 && start + i < count; ++i) {
        const Item &item = state.checklist.items[start + i];
        char text[kText + 8];
        std::snprintf(text, sizeof(text), "%s %s", item.done ? "[x]" : "[ ]",
                      item.text);
        ui_pixel_label_set_text(rows[i], text);
        ui_pixel_text_color(rows[i], start + i == state.selected
                                         ? UI_ON_ACCENT : UI_TEXT);
        ui_pixel_bg_color(rows[i], start + i == state.selected
                                      ? UI_ACCENT : UI_SURFACE);
        lv_obj_remove_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
    }
    set_footer("上下选择 · 确定完成", "长按下删除 · 长按上清已完成");
}

void start() {
    if (!bsp_lvgl_lock(1000)) return;
    view = new (std::nothrow) View;
    message = nullptr;
    screen = lv_obj_create(nullptr);
    ui_pixel_background(screen);
    lv_obj_t *top = ui_pixel_top_bar(screen);
    lv_obj_t *name = ui_pixel_label(top, "事项清单", &buddy_font_16, UI_TEXT);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 12, 0);
    battery = ui_pixel_battery_create(top, UI_STATUS_BATTERY_X, 6, UI_TEXT,
                                      &lv_font_montserrat_14);
    heading = label("", 12, 42, 216, 26, UI_ACCENT);
    content = label("", 12, 80, 216, 168, UI_TEXT);
    for (unsigned i = 0; i < 3; ++i) {
        rows[i] = label("", 12, 76 + static_cast<int>(i) * 60, 216, 54,
                        UI_TEXT);
        lv_label_set_long_mode(rows[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_bg_opa(rows[i], LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(rows[i], 5, 0);
    }
    footer = ui_pixel_footer(screen, "", "", &buddy_font_16);
    footer_secondary = lv_obj_get_child(screen, -1);
    lv_screen_load(screen);
    refresh();
    timer = lv_timer_create([](lv_timer_t *) { refresh(); }, 1000, nullptr);
    bsp_lvgl_unlock();
}

void stop() {
    if (!bsp_lvgl_lock(1000)) return;
    if (timer) {
        lv_timer_delete(timer);
        timer = nullptr;
    }
    if (screen) {
        lv_obj_delete(screen);
        screen = nullptr;
    }
    delete view;
    view = nullptr;
    bsp_lvgl_unlock();
}

bool back() {
    if (!bsp_lvgl_lock(500)) return true;
    const bool handled =
        view && (view->delete_selected || view->clear_completed);
    if (handled) {
        view->delete_selected = false;
        view->clear_completed = false;
        message = nullptr;
        refresh();
    }
    bsp_lvgl_unlock();
    return handled;
}

void key(uint8_t button, app_btn_event_t event, uint16_t) {
    if (!view || !bsp_lvgl_lock(500)) return;
    View &state = *view;
    bool refresh_now = false;
    bool voice = false;
    bool modify_now = false;
    Action action = Action::Set;
    uint32_t item_id = 0;
    int done = -1;
    message = nullptr;

    if (event == BTN_EVT_LONG &&
        !state.delete_selected && !state.clear_completed) {
        if (button == BSP_BTN_DOWN && state.checklist.count) {
            state.delete_selected = true;
            refresh_now = true;
        } else if (button == BSP_BTN_UP) {
            if (completed_count(state.checklist)) {
                state.clear_completed = true;
            } else {
                message = "还没有已完成事项";
            }
            refresh_now = true;
        }
    } else if (event == BTN_EVT_CLICK) {
        if (state.delete_selected || state.clear_completed) {
            if (button == BSP_BTN_UP) {
                state.delete_selected = false;
                state.clear_completed = false;
                refresh_now = true;
            } else if (button == BSP_BTN_OK) {
                modify_now = true;
                if (state.delete_selected) {
                    action = Action::Remove;
                    item_id = state.checklist.items[state.selected].id;
                } else {
                    action = Action::ClearDone;
                }
            }
        } else if (!state.checklist.count) {
            voice = button == BSP_BTN_OK;
        } else if (button == BSP_BTN_OK) {
            modify_now = true;
            action = Action::Set;
            item_id = state.checklist.items[state.selected].id;
            done = !state.checklist.items[state.selected].done;
        } else if (button == BSP_BTN_DOWN) {
            state.selected = (state.selected + 1) % state.checklist.count;
            refresh_now = true;
        } else if (button == BSP_BTN_UP) {
            state.selected =
                (state.selected + state.checklist.count - 1) % state.checklist.count;
            refresh_now = true;
        }
    }

    if (refresh_now) {
        refresh();
        bsp_lvgl_unlock();
        return;
    }
    if (voice) {
        bsp_lvgl_unlock();
        app_registry_switch_to(APP_ID_XIAOZHI);
        return;
    }
    if (!modify_now) {
        bsp_lvgl_unlock();
        return;
    }

    const uint32_t expected = state.checklist.revision;
    bsp_lvgl_unlock();
    const char *error = nullptr;
    const bool changed =
        modify(expected, action, item_id, nullptr, done, nullptr, 0, &error);
    if (bsp_lvgl_lock(500)) {
        if (view) {
            if (changed) {
                view->delete_selected = false;
                view->clear_completed = false;
                view->revision = UINT32_MAX;
            }
            message = error;
            refresh();
        }
        bsp_lvgl_unlock();
    }
}

}  // namespace
}  // namespace lists

extern const passport_app_t g_lists_app;
const passport_app_t g_lists_app = {
    .id = APP_ID_LISTS,
    .name = "事项清单",
    .en_name = "CHECKLIST",
    .desc = "一个清单\n记录全部事项",
    .tag = "本地文字",
    .theme_color = 0x8CF5A6,
    .init = nullptr,
    .start = lists::start,
    .stop = lists::stop,
    .on_key = lists::key,
    .prepare_stop = nullptr,
    .on_back = lists::back,
};
