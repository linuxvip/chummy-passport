#include "apps/calendar/app_calendar.h"

#include "app_fonts.h"
#include "apps/bazi/bazi_almanac.h"
#include "apps/calendar/calendar_core.h"
#include "apps/voice/time_sync.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "launcher/app_registry.h"
#include "ui_pixel.h"
#include "wifi_manager.h"

#include <cstdio>
#include <cstring>
#include <ctime>

namespace calendar {
namespace {

constexpr char kTag[] = "calendar";
constexpr int kCellW = 32;
constexpr int kCellH = 37;
constexpr int kGridX = 8;
constexpr int kGridY = 68;
constexpr int kVisibleRows = 5;
constexpr uint8_t kNoTerm = UINT8_MAX;

enum class Mode : uint8_t { Month, Detail, Picker };
enum class PickerField : uint8_t { Year, Month, Jump, Cancel, Count };

struct Cell {
    lv_obj_t *box = nullptr;
    lv_obj_t *day = nullptr;
    lv_obj_t *sub = nullptr;
    lv_obj_t *ganzhi = nullptr;
};

Mode s_mode = Mode::Month;
int s_year = 2026;
int s_month = 1;
int s_day = 1;
int s_today_year = 2026;
int s_today_month = 1;
int s_today_day = 1;
uint8_t s_grid_offset = 0;
PickerField s_picker_field = PickerField::Year;
bool s_picker_editing = false;
int s_picker_year = 2026;
int s_picker_month = 1;

lv_obj_t *s_screen = nullptr;
ui_pixel_battery_t s_battery{};
lv_obj_t *s_wifi_bars[3] = {nullptr, nullptr, nullptr};
lv_obj_t *s_month_title = nullptr;
lv_obj_t *s_grid = nullptr;
Cell s_cells[42] = {};
lv_obj_t *s_picker_rows[static_cast<int>(PickerField::Count)] = {};
lv_obj_t *s_picker_names[static_cast<int>(PickerField::Count)] = {};
lv_obj_t *s_picker_values[static_cast<int>(PickerField::Count)] = {};

const char *const kWeekdays[] = {
    "星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六",
};

const char *const kPickerNames[] = {"年份", "月份", "跳转", "取消"};

void set_text(lv_obj_t *obj, const char *text) {
    if (obj && text && std::strcmp(lv_label_get_text(obj), text) != 0)
        ui_pixel_label_set_text(obj, text);
}

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                int x, int y, int w, int h,
                lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
    lv_obj_t *obj = ui_pixel_label(parent, "", font, color);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_text_align(obj, align, 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_MODE_DOTS);
    return obj;
}

bool local_today(int *year, int *month, int *day) {
    time_t now = time(nullptr);
    if (now < 1700000000) return false;
    now += time_sync_tz_offset_sec();
    struct tm tm {};
    gmtime_r(&now, &tm);
    *year = tm.tm_year + 1900;
    *month = tm.tm_mon + 1;
    *day = tm.tm_mday;
    return *year >= CALENDAR_FIRST_YEAR && *year <= CALENDAR_LAST_YEAR;
}

void update_status_icons() {
    ui_pixel_battery_set(&s_battery, bsp_battery_soc());
    int bars = 0;
    uint32_t color = UI_TEXT_DIM;
    if (WifiManager::GetInstance().IsConnected()) {
        const int rssi = WifiManager::GetInstance().GetRssi();
        bars = rssi >= -60 ? 3 : rssi >= -75 ? 2 : 1;
        color = UI_TEXT;
    }
    ui_pixel_wifi_bars_set(s_wifi_bars, bars, color);
}

void add_top_bar(const char *title) {
    lv_obj_t *top = ui_pixel_top_bar(s_screen);
    ui_pixel_wifi_bars_create(top, UI_STATUS_WIFI_X, 18, s_wifi_bars);
    lv_obj_t *name = label(top, &buddy_font_16, UI_TEXT, 0, 0, 118, 24);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 42, 0);
    set_text(name, title);
    s_battery = ui_pixel_battery_create(
        top, UI_STATUS_BATTERY_X, 6, UI_TEXT, &lv_font_montserrat_14);
    update_status_icons();
}

void close_screen() {
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = nullptr;
    }
    s_battery = {};
    s_month_title = nullptr;
    s_grid = nullptr;
    for (auto &cell : s_cells) cell = {};
    for (auto &label : s_picker_rows) label = nullptr;
    for (auto &label : s_picker_names) label = nullptr;
    for (auto &label : s_picker_values) label = nullptr;
    for (auto &bar : s_wifi_bars) bar = nullptr;
}

bool same_date(int y, int m, int d, int ty, int tm, int td) {
    return y == ty && m == tm && d == td;
}

void refresh_month() {
    if (!s_screen || !s_month_title || !s_grid) return;
    char title[24];
    std::snprintf(title, sizeof(title), "%04d年 %02d月", s_year, s_month);
    set_text(s_month_title, title);

    const uint8_t days = calendar_days_in_month(s_year, s_month);
    const uint8_t first_weekday = calendar_weekday(s_year, s_month, 1);
    const int selected_index = first_weekday + s_day - 1;
    const uint8_t selected_row = (uint8_t)(selected_index / 7);
    s_grid_offset = selected_row >= kVisibleRows ? 1 : 0;

    for (int index = 0; index < 42; ++index) {
        Cell &cell = s_cells[index];
        const int date_day = index - first_weekday + 1;
        if (date_day < 1 || date_day > days) {
            lv_obj_add_flag(cell.box, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(cell.box, LV_OBJ_FLAG_HIDDEN);
        const int row = index / 7;
        const int col = index % 7;
        lv_obj_set_pos(cell.box, col * kCellW,
                       (row - s_grid_offset) * kCellH);

        calendar_day_t info;
        if (!calendar_day_info(s_year, s_month, date_day, &info)) {
            lv_obj_add_flag(cell.box, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        char day_text[8];
        std::snprintf(day_text, sizeof(day_text), "%d", date_day);
        const bool selected = date_day == s_day;
        const bool today = same_date(s_year, s_month, date_day,
                                     s_today_year, s_today_month, s_today_day);
        set_text(cell.day, day_text);
        if (info.solar_term_index != kNoTerm) {
            set_text(cell.sub, calendar_solar_term_name(info.solar_term_index));
            ui_pixel_text_color(cell.sub, UI_DANGER);
        } else {
            set_text(cell.sub, info.lunar_cell);
            ui_pixel_text_color(cell.sub, UI_TEXT_DIM);
        }
        set_text(cell.ganzhi, info.day_ganzhi);
        ui_pixel_text_color(cell.day, selected || today ? UI_ACCENT : UI_TEXT);
        ui_pixel_bg_color(cell.box, selected ? UI_SURFACE_ALT : UI_BG);
        lv_obj_set_style_border_width(cell.box, selected ? 1 : 1, 0);
        lv_obj_set_style_border_side(cell.box,
            static_cast<lv_border_side_t>(selected ? LV_BORDER_SIDE_FULL :
                (LV_BORDER_SIDE_RIGHT | LV_BORDER_SIDE_BOTTOM)), 0);
        lv_obj_set_style_border_color(cell.box,
            lv_color_hex(selected ? UI_ACCENT : UI_BORDER), 0);
    }
}

void build_month() {
    close_screen();
    s_mode = Mode::Month;
    s_screen = lv_obj_create(nullptr);
    ui_pixel_background(s_screen);
    add_top_bar("万年历");

    s_month_title = label(s_screen, &ganzhi_font_12, UI_ACCENT,
                          12, 28, 216, 18, LV_TEXT_ALIGN_CENTER);
    const char *weekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
    for (int col = 0; col < 7; ++col) {
        lv_obj_t *weekday = label(s_screen, &calendar_font_9, UI_TEXT_DIM,
                                  kGridX + col * kCellW, 48,
                                  kCellW, 15, LV_TEXT_ALIGN_CENTER);
        set_text(weekday, weekdays[col]);
    }

    s_grid = lv_obj_create(s_screen);
    lv_obj_remove_flag(s_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_grid, kGridX, kGridY);
    lv_obj_set_size(s_grid, 7 * kCellW, kVisibleRows * kCellH);
    lv_obj_set_style_bg_opa(s_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_grid, 0, 0);
    lv_obj_set_style_radius(s_grid, 0, 0);
    lv_obj_set_style_pad_all(s_grid, 0, 0);

    for (auto &cell : s_cells) {
        cell.box = lv_obj_create(s_grid);
        lv_obj_remove_flag(cell.box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(cell.box, kCellW, kCellH);
        lv_obj_set_style_radius(cell.box, 0, 0);
        lv_obj_set_style_pad_all(cell.box, 0, 0);
        lv_obj_set_style_bg_opa(cell.box, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(cell.box, lv_color_hex(UI_BG), 0);
        lv_obj_set_style_border_width(cell.box, 1, 0);
        lv_obj_set_style_border_color(cell.box, lv_color_hex(UI_BORDER), 0);
        cell.day = label(cell.box, &ganzhi_font_12, UI_TEXT,
                         0, 0, kCellW, 15, LV_TEXT_ALIGN_CENTER);
        cell.sub = label(cell.box, &calendar_font_9, UI_TEXT_DIM,
                         0, 12, kCellW, 12, LV_TEXT_ALIGN_CENTER);
        cell.ganzhi = label(cell.box, &calendar_font_9, UI_TEXT,
                            0, 24, kCellW, 12, LV_TEXT_ALIGN_CENTER);
    }

    lv_obj_t *legend = label(s_screen, &buddy_font_16, UI_TEXT_DIM,
                             12, 256, 216, 16);
    set_text(legend, "日 / 农历或节气 / 日干支");
    ui_pixel_footer(s_screen, "上下选日 · 确定详情",
                    "长按上今天 · 长按下年月", &buddy_font_16);
    refresh_month();
    lv_screen_load(s_screen);
}

void build_detail() {
    close_screen();
    s_mode = Mode::Detail;
    s_screen = lv_obj_create(nullptr);
    ui_pixel_background(s_screen);
    add_top_bar("日期详情");

    calendar_day_t info;
    if (!calendar_day_info(s_year, s_month, s_day, &info)) {
        ui_pixel_footer(s_screen, "日期无效", "确定返回月历", &buddy_font_16);
        lv_screen_load(s_screen);
        return;
    }

    char text[64];
    std::snprintf(text, sizeof(text), "%04d年%02d月%02d日", s_year, s_month, s_day);
    lv_obj_t *date = label(s_screen, &buddy_font_16, UI_TEXT,
                           12, 34, 216, 18, LV_TEXT_ALIGN_CENTER);
    set_text(date, text);
    lv_obj_t *weekday = label(s_screen, &buddy_font_16, UI_TEXT_DIM,
                              12, 53, 216, 18, LV_TEXT_ALIGN_CENTER);
    set_text(weekday, kWeekdays[info.weekday]);

    lv_obj_t *day_gz = label(s_screen, &ganzhi_font_20, UI_ACCENT,
                             12, 78, 216, 24, LV_TEXT_ALIGN_CENTER);
    set_text(day_gz, info.day_ganzhi);
    lv_obj_t *lunar = label(s_screen, &buddy_font_16, UI_TEXT,
                            12, 108, 216, 18, LV_TEXT_ALIGN_CENTER);
    set_text(lunar, info.lunar_full);

    std::snprintf(text, sizeof(text), "年 %s    月 %s    日 %s",
                  info.year_ganzhi, info.month_ganzhi, info.day_ganzhi);
    lv_obj_t *pillars = label(s_screen, &ganzhi_font_12, UI_TEXT,
                              8, 135, 224, 16, LV_TEXT_ALIGN_CENTER);
    set_text(pillars, text);

    if (info.solar_term_index != kNoTerm) {
        std::snprintf(text, sizeof(text), "今日交%s",
                      calendar_solar_term_name(info.solar_term_index));
        lv_obj_t *term = label(s_screen, &buddy_font_16, UI_DANGER,
                               12, 162, 216, 18, LV_TEXT_ALIGN_CENTER);
        set_text(term, text);
    }
    char next_name[10];
    uint8_t days;
    if (calendar_next_solar_term(s_year, s_month, s_day, next_name, &days)) {
        std::snprintf(text, sizeof(text), "距%s %u天",
                      next_name, (unsigned)days);
        lv_obj_t *next = label(s_screen, &buddy_font_16, UI_TEXT_DIM,
                               12, 188, 216, 18, LV_TEXT_ALIGN_CENTER);
        set_text(next, text);
    }
    ui_pixel_footer(s_screen, "上下看前后一天", "确定返回月历", &buddy_font_16);
    lv_screen_load(s_screen);
}

void refresh_picker() {
    if (!s_screen) return;
    for (int i = 0; i < static_cast<int>(PickerField::Count); ++i) {
        const bool selected = i == static_cast<int>(s_picker_field);
        char value[24] = {};
        if (i == static_cast<int>(PickerField::Year)) {
            if (selected && s_picker_editing)
                std::snprintf(value, sizeof(value), "< %d >", s_picker_year);
            else
                std::snprintf(value, sizeof(value), "%d", s_picker_year);
        } else if (i == static_cast<int>(PickerField::Month)) {
            if (selected && s_picker_editing)
                std::snprintf(value, sizeof(value), "< %d月 >", s_picker_month);
            else
                std::snprintf(value, sizeof(value), "%d月", s_picker_month);
        }
        set_text(s_picker_names[i], kPickerNames[i]);
        set_text(s_picker_values[i], value);
        ui_pixel_bg_color(s_picker_rows[i],
            selected ? (s_picker_editing ? UI_WARNING : UI_ACCENT) : UI_BG);
        ui_pixel_text_color(s_picker_names[i], selected ? UI_ON_ACCENT : UI_TEXT);
        ui_pixel_text_color(s_picker_values[i], selected ? UI_ON_ACCENT : UI_TEXT_DIM);
    }
}

void build_picker() {
    close_screen();
    s_mode = Mode::Picker;
    s_picker_year = s_year;
    s_picker_month = s_month;
    s_picker_field = PickerField::Year;
    s_picker_editing = false;
    s_screen = lv_obj_create(nullptr);
    ui_pixel_background(s_screen);
    add_top_bar("选择年月");
    lv_obj_t *header = label(s_screen, &buddy_font_16, UI_TEXT_DIM,
                             12, 30, 216, 18);
    set_text(header, "上下选择 · 确定修改");
    for (int i = 0; i < static_cast<int>(PickerField::Count); ++i) {
        lv_obj_t *row = lv_obj_create(s_screen);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(row, 12, 52 + i * 31);
        lv_obj_set_size(row, 216, 28);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        s_picker_rows[i] = row;
        s_picker_names[i] = label(row, &buddy_font_16, UI_TEXT,
                                   6, 3, 120, 22);
        s_picker_values[i] = label(row, &buddy_font_16, UI_TEXT_DIM,
                                   118, 3, 92, 22, LV_TEXT_ALIGN_RIGHT);
    }
    ui_pixel_footer(s_screen, "上下选择 · 确定修改", "长按下键跳转",
                    &buddy_font_16);
    refresh_picker();
    lv_screen_load(s_screen);
}

void change_day(int delta) {
    const int64_t absolute = bazi_almanac_days_from_civil(s_year, s_month, s_day) + delta;
    int year = s_year;
    int month = s_month;
    int day = s_day;
    bazi_almanac_civil_from_days(absolute, &year, &month, &day);
    if (year < CALENDAR_FIRST_YEAR || year > CALENDAR_LAST_YEAR) return;
    const bool month_changed = year != s_year || month != s_month;
    s_year = year;
    s_month = month;
    s_day = day;
    if (s_mode == Mode::Detail)
        build_detail();
    else if (month_changed)
        build_month();
    else
        refresh_month();
}

void jump_to_today() {
    int y, m, d;
    if (!local_today(&y, &m, &d)) return;
    s_today_year = s_year = y;
    s_today_month = s_month = m;
    s_today_day = s_day = d;
    if (s_mode == Mode::Detail)
        build_detail();
    else
        build_month();
}

void apply_picker() {
    s_year = s_picker_year;
    s_month = s_picker_month;
    const uint8_t days = calendar_days_in_month(s_year, s_month);
    if (s_day > days) s_day = days;
    build_month();
}

void adjust_picker(int delta) {
    if (s_picker_field == PickerField::Year) {
        int value = s_picker_year + delta;
        if (value < CALENDAR_FIRST_YEAR) value = CALENDAR_FIRST_YEAR;
        if (value > CALENDAR_LAST_YEAR) value = CALENDAR_LAST_YEAR;
        s_picker_year = value;
    } else if (s_picker_field == PickerField::Month) {
        int value = s_picker_month + delta;
        if (value < 1) value = 12;
        if (value > 12) value = 1;
        s_picker_month = value;
    }
}

void picker_key(uint8_t btn, app_btn_event_t event) {
    if (event == BTN_EVT_LONG && btn == BSP_BTN_DOWN) {
        apply_picker();
        return;
    }
    if (event != BTN_EVT_CLICK) return;
    const int field = static_cast<int>(s_picker_field);
    if (s_picker_editing) {
        if (btn == BSP_BTN_UP) adjust_picker(1);
        else if (btn == BSP_BTN_DOWN) adjust_picker(-1);
        else if (btn == BSP_BTN_OK) {
            s_picker_editing = false;
            if (s_picker_field == PickerField::Year)
                s_picker_field = PickerField::Month;
            else if (s_picker_field == PickerField::Month)
                s_picker_field = PickerField::Jump;
        }
    } else if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        int next = field + (btn == BSP_BTN_DOWN ? 1 : -1);
        if (next < 0) next = static_cast<int>(PickerField::Count) - 1;
        if (next >= static_cast<int>(PickerField::Count)) next = 0;
        s_picker_field = static_cast<PickerField>(next);
    } else if (btn == BSP_BTN_OK) {
        if (s_picker_field == PickerField::Year ||
            s_picker_field == PickerField::Month) {
            s_picker_editing = true;
        } else if (s_picker_field == PickerField::Jump) {
            apply_picker();
            return;
        } else {
            build_month();
            return;
        }
    }
    refresh_picker();
}

void month_key(uint8_t btn, app_btn_event_t event) {
    if (event == BTN_EVT_LONG) {
        if (btn == BSP_BTN_UP) jump_to_today();
        else if (btn == BSP_BTN_DOWN) build_picker();
        return;
    }
    if (event != BTN_EVT_CLICK) return;
    if (btn == BSP_BTN_UP) change_day(-1);
    else if (btn == BSP_BTN_DOWN) change_day(1);
    else if (btn == BSP_BTN_OK) build_detail();
}

void detail_key(uint8_t btn, app_btn_event_t event) {
    if (event == BTN_EVT_LONG) {
        if (btn == BSP_BTN_UP) jump_to_today();
        else if (btn == BSP_BTN_DOWN) build_picker();
        return;
    }
    if (event != BTN_EVT_CLICK) return;
    if (btn == BSP_BTN_UP) change_day(-1);
    else if (btn == BSP_BTN_DOWN) change_day(1);
    else if (btn == BSP_BTN_OK) build_month();
}

void start() {
    if (!bsp_lvgl_lock(1000)) return;
    if (s_screen) {
        bsp_lvgl_unlock();
        return;
    }
    if (!local_today(&s_today_year, &s_today_month, &s_today_day)) {
        s_today_year = 2026;
        s_today_month = 1;
        s_today_day = 1;
    }
    s_year = s_today_year;
    s_month = s_today_month;
    s_day = s_today_day;
    build_month();
    ESP_LOGI(kTag, "started; free=%u largest=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    bsp_lvgl_unlock();
}

void stop() {
    if (!bsp_lvgl_lock(-1)) return;
    close_screen();
    s_mode = Mode::Month;
    ESP_LOGI(kTag, "stopped; free=%u", (unsigned)esp_get_free_heap_size());
    bsp_lvgl_unlock();
}

void on_key(uint8_t btn, app_btn_event_t event, uint16_t) {
    if (!bsp_lvgl_lock(500)) return;
    if (s_mode == Mode::Month) month_key(btn, event);
    else if (s_mode == Mode::Detail) detail_key(btn, event);
    else picker_key(btn, event);
    bsp_lvgl_unlock();
}

bool on_back() {
    if (s_mode == Mode::Month) return false;
    if (!bsp_lvgl_lock(500)) return true;
    build_month();
    bsp_lvgl_unlock();
    return true;
}

bool prepare_stop() {
    return true;
}

} // namespace
} // namespace calendar

extern const passport_app_t g_calendar_app = {
    .id = APP_ID_CALENDAR,
    .name = "万年历",
    .en_name = "CALENDAR",
    .desc = "农历 · 节气 · 日干支",
    .tag = "本地历法",
    .theme_color = 0xDDBA72,
    .init = nullptr,
    .start = calendar::start,
    .stop = calendar::stop,
    .on_key = calendar::on_key,
    .prepare_stop = calendar::prepare_stop,
    .on_back = calendar::on_back,
};
