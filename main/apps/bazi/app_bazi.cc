#include "apps/app_base.h"
#include "apps/bazi/bazi_core.h"
#include "apps/voice/time_sync.h"
#include "app_fonts.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "launcher/app_registry.h"
#include "launcher/settings.h"
#include "ui_pixel.h"
#include "wifi_manager.h"

#include <cstdio>
#include <cstring>
#include <ctime>

namespace bazi {
namespace {

constexpr char kTag[] = "bazi";
constexpr uint8_t kVisibleLuck = 8;
constexpr uint8_t kVisibleRows = 2;
constexpr uint8_t kEditorRows = 8;

enum class Mode : uint8_t { Chart, Editor };
enum class Field : uint8_t {
    Sex, Calendar, Year, Month, Day, Hour, Minute, Leap,
    City, ZiRule, TimeMode, Calculate, Count
};

struct Cell {
    lv_obj_t *box = nullptr;
    lv_obj_t *a = nullptr;
    lv_obj_t *b = nullptr;
};

Mode s_mode = Mode::Chart;
bazi_birth_t s_birth{};
bazi_chart_t s_chart{};
bool s_has_chart = false;
bool s_form_initialized = false;
lv_obj_t *s_screen = nullptr;
ui_pixel_battery_t s_battery{};
lv_obj_t *s_wifi_bars[3] = {nullptr, nullptr, nullptr};
lv_obj_t *s_date = nullptr;
lv_obj_t *s_meta = nullptr;
lv_obj_t *s_method = nullptr;
lv_obj_t *s_stems[4] = {};
lv_obj_t *s_branches[4] = {};
lv_obj_t *s_luck_header_left = nullptr;
lv_obj_t *s_luck_header_right = nullptr;
Cell s_luck_cells[kVisibleLuck] = {};
Cell s_flow_cells[kVisibleRows][kVisibleLuck] = {};
lv_obj_t *s_editor_header = nullptr;
lv_obj_t *s_editor_rows[kEditorRows] = {};
lv_obj_t *s_editor_names[kEditorRows] = {};
lv_obj_t *s_editor_values[kEditorRows] = {};
lv_obj_t *s_footer_primary = nullptr;
uint8_t s_flow_row = 0;
uint8_t s_editor_selected = 0;
bool s_editor_editing = false;
char s_editor_message[64] = {};
uint8_t s_current_luck = 0;
uint8_t s_current_flow_row = 0;
uint8_t s_luck_first = 0;

const char *const kMonthNames[] = {
    "", "正", "二", "三", "四", "五", "六",
    "七", "八", "九", "十", "十一", "十二",
};
const char *const kDayNames[] = {
    "", "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十",
    "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十",
    "廿一", "廿二", "廿三", "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十",
};

void set_text(lv_obj_t *obj, const char *text) {
    if (obj && text && std::strcmp(lv_label_get_text(obj), text) != 0)
        ui_pixel_label_set_text(obj, text);
}

lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                uint32_t color, int x, int y, int w, int h,
                lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
    lv_obj_t *obj = ui_pixel_label(parent, text, font, color);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(obj, align, 0);
    return obj;
}

lv_obj_t *cell(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(UI_SURFACE), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(UI_BORDER), 0);
    return obj;
}

uint32_t element_color(uint8_t element) {
    if (element >= 5) return UI_TEXT_DIM;
    return ui_theme_element_color(static_cast<ui_element_t>(element));
}

void ganzhi_text(bazi_ganzhi_t value, char out[7]) {
    std::snprintf(out, 7, "%s%s", bazi_stem_text(value.stem),
                  bazi_branch_text(value.branch));
}

bool local_date(int *year, int *month, int *day) {
    time_t now = time(nullptr);
    if (now < 1700000000) return false;
    now += time_sync_tz_offset_sec();
    struct tm tm {};
    gmtime_r(&now, &tm);
    *year = tm.tm_year + 1900;
    *month = tm.tm_mon + 1;
    *day = tm.tm_mday;
    return true;
}

void default_birth(bazi_birth_t *birth) {
    int year = 1990;
    int month = 1;
    int day = 1;
    *birth = {
        .version = 1,
        .calendar = BAZI_CALENDAR_SOLAR,
        .sex = BAZI_SEX_MALE,
        .zi_rule = settings_get_late_zi() ? BAZI_ZI_LATE : BAZI_ZI_EARLY,
        .time_mode = BAZI_TIME_TRUE_SOLAR,
        .lunar_leap = 0,
        .city_id = 0,
        .reserved = 0,
        .year = (int16_t)year,
        .month = (uint8_t)month,
        .day = (uint8_t)day,
        .hour = 12,
        .minute = 0,
        .longitude_e4 = bazi_city_longitude_e4(0),
    };
}

bool calculate_chart() {
    bazi_chart_t chart;
    if (!bazi_core_create(&s_birth, &chart)) {
        std::snprintf(s_editor_message, sizeof(s_editor_message),
                      "资料无效，请检查日期");
        return false;
    }
    s_chart = chart;
    s_has_chart = true;
    return true;
}

void update_status_icons() {
    ui_pixel_battery_set(&s_battery, bsp_battery_soc());
    int bars = 0;
    uint32_t color = UI_TEXT_DIM;
    if (WifiManager::GetInstance().IsConnected()) {
        const int rssi = WifiManager::GetInstance().GetRssi();
        bars = rssi >= -60 ? 3 : (rssi >= -75 ? 2 : 1);
        color = UI_TEXT;
    }
    ui_pixel_wifi_bars_set(s_wifi_bars, bars, color);
}

void add_top_bar(const char *title) {
    lv_obj_t *top = ui_pixel_top_bar(s_screen);
    ui_pixel_wifi_bars_create(top, UI_STATUS_WIFI_X, 18, s_wifi_bars);
    lv_obj_t *name = label(top, title, &buddy_font_16, UI_TEXT, 0, 0, 108, 24);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 42, 0);
    s_battery = ui_pixel_battery_create(
        top, UI_STATUS_BATTERY_X, 6, UI_TEXT, &lv_font_montserrat_14);
    update_status_icons();
}

void initialize_chart_position() {
    int year = 2026, month = 1, day = 1;
    local_date(&year, &month, &day);
    s_current_luck = bazi_current_luck_index(&s_chart, year);
    s_current_flow_row = bazi_flow_row_for_year(&s_chart, s_current_luck, year);
    s_luck_first = s_current_luck > 2 ? s_current_luck - 2 : 0;
    if (s_luck_first + kVisibleLuck > BAZI_LUCK_COUNT)
        s_luck_first = BAZI_LUCK_COUNT - kVisibleLuck;
    s_flow_row = (uint8_t)((s_current_flow_row / kVisibleRows) * kVisibleRows);
    if (s_flow_row > BAZI_YEARS_PER_LUCK - kVisibleRows)
        s_flow_row = BAZI_YEARS_PER_LUCK - kVisibleRows;
}

void refresh_luck_table() {
    if (!s_screen || !s_has_chart) return;
    char text[48];
    std::snprintf(text, sizeof(text), "大运  顺排 / %d 起",
                  s_chart.luck.step[s_luck_first].start_year);
    if (!s_chart.luck.forward)
        std::snprintf(text, sizeof(text), "大运  逆排 / %d 起",
                      s_chart.luck.step[s_luck_first].start_year);
    set_text(s_luck_header_left, text);
    std::snprintf(text, sizeof(text), "流年 第%u-%u年",
                  (unsigned)(s_flow_row + 1),
                  (unsigned)(s_flow_row + kVisibleRows));
    set_text(s_luck_header_right, text);

    for (uint8_t col = 0; col < kVisibleLuck; ++col) {
        Cell &target = s_luck_cells[col];
        const uint8_t index = (uint8_t)(s_luck_first + col);
        const bool valid = index < BAZI_LUCK_COUNT;
        const bool current = valid && index == s_current_luck;
        set_text(target.a, valid ? bazi_stem_text(s_chart.luck.step[index].ganzhi.stem) : "");
        set_text(target.b, valid ? bazi_branch_text(s_chart.luck.step[index].ganzhi.branch) : "");
        ui_pixel_text_color(target.a, valid ? element_color(
            bazi_element_of_stem(s_chart.luck.step[index].ganzhi.stem)) : UI_TEXT_DIM);
        ui_pixel_text_color(target.b, valid ? element_color(
            bazi_element_of_branch(s_chart.luck.step[index].ganzhi.branch)) : UI_TEXT_DIM);
        ui_pixel_bg_color(target.box, current ? UI_SURFACE_ALT : UI_SURFACE);
        lv_obj_set_style_border_color(target.box,
            lv_color_hex(current ? UI_ACCENT : UI_BORDER), 0);
    }

    for (uint8_t row = 0; row < kVisibleRows; ++row) {
        const uint8_t flow_index = (uint8_t)(s_flow_row + row);
        for (uint8_t col = 0; col < kVisibleLuck; ++col) {
            Cell &target = s_flow_cells[row][col];
            const uint8_t luck_index = (uint8_t)(s_luck_first + col);
            if (luck_index >= BAZI_LUCK_COUNT || flow_index >= BAZI_YEARS_PER_LUCK) {
                set_text(target.a, "");
                continue;
            }
            const int year = s_chart.luck.step[luck_index].start_year + flow_index;
            const bazi_ganzhi_t gz = bazi_flow_year(year);
            char flow[7];
            ganzhi_text(gz, flow);
            set_text(target.a, flow);
            ui_pixel_text_color(target.a, UI_TEXT);
            const bool current = luck_index == s_current_luck &&
                                 flow_index == s_current_flow_row;
            ui_pixel_bg_color(target.box, current ? UI_SURFACE_ALT : UI_SURFACE);
            lv_obj_set_style_border_color(target.box,
                lv_color_hex(current ? UI_ACCENT : UI_BORDER), 0);
        }
    }
}

void build_chart() {
    s_mode = Mode::Chart;
    s_screen = lv_obj_create(nullptr);
    ui_pixel_background(s_screen);
    add_top_bar("八字排盘");
    s_date = label(s_screen, "", &lv_font_montserrat_14, UI_TEXT, 12, 32, 160, 16);
    s_method = label(s_screen, "", &ganzhi_font_12, UI_ACCENT, 174, 34, 54, 14,
                     LV_TEXT_ALIGN_RIGHT);
    s_meta = label(s_screen, "", &buddy_font_16, UI_TEXT_DIM, 12, 48, 216, 18);

    for (int col = 0; col < 4; ++col) {
        const int x = 12 + col * 54;
        s_stems[col] = label(s_screen, "", &ganzhi_font_20, UI_TEXT,
                             x, 74, 54, 24, LV_TEXT_ALIGN_CENTER);
        s_branches[col] = label(s_screen, "", &ganzhi_font_20, UI_TEXT,
                                x, 100, 54, 24, LV_TEXT_ALIGN_CENTER);
    }
    s_luck_header_left = label(s_screen, "", &ganzhi_font_12, UI_TEXT_DIM,
                               12, 132, 130, 14);
    s_luck_header_right = label(s_screen, "", &ganzhi_font_12, UI_TEXT_DIM,
                                140, 132, 88, 14, LV_TEXT_ALIGN_RIGHT);
    for (uint8_t col = 0; col < kVisibleLuck; ++col) {
        Cell &target = s_luck_cells[col];
        target.box = cell(s_screen, 12 + col * 27, 147, 27, 38);
        target.a = ui_pixel_label(target.box, "", &ganzhi_font_13, UI_TEXT);
        target.b = ui_pixel_label(target.box, "", &ganzhi_font_13, UI_TEXT);
        lv_obj_set_style_text_align(target.a, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_align(target.b, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(target.a, 0, 10);
        lv_obj_set_size(target.a, 13, 17);
        lv_obj_set_pos(target.b, 14, 10);
        lv_obj_set_size(target.b, 13, 17);
    }
    for (uint8_t row = 0; row < kVisibleRows; ++row) {
        for (uint8_t col = 0; col < kVisibleLuck; ++col) {
            Cell &target = s_flow_cells[row][col];
            target.box = cell(s_screen, 12 + col * 27, 185 + row * 30, 27, 29);
            target.a = ui_pixel_label(target.box, "", &ganzhi_font_12, UI_TEXT);
            lv_obj_center(target.a);
        }
    }
    s_footer_primary = ui_pixel_footer(
        s_screen, "下键下两行 · 上键回看", "长按确定改资料", &buddy_font_16);

    char text[32];
    std::snprintf(text, sizeof(text), "%04d-%02u-%02u %02u:%02u",
                  s_chart.solar_year, s_chart.solar_month, s_chart.solar_day,
                  s_chart.solar_hour, s_chart.solar_minute);
    set_text(s_date, text);
    set_text(s_method, s_birth.time_mode == BAZI_TIME_TRUE_SOLAR ? "真太阳时" : "标准时");
    std::snprintf(text, sizeof(text), "%s  男  农历%s%s月%s",
                  bazi_city_name(s_birth.city_id),
                  s_chart.lunar_month_leap ? "闰" : "",
                  kMonthNames[s_chart.lunar_month],
                  kDayNames[s_chart.lunar_day]);
    if (s_birth.sex == BAZI_SEX_FEMALE)
        std::snprintf(text, sizeof(text), "%s  女  农历%s%s月%s",
                      bazi_city_name(s_birth.city_id),
                      s_chart.lunar_month_leap ? "闰" : "",
                      kMonthNames[s_chart.lunar_month],
                      kDayNames[s_chart.lunar_day]);
    set_text(s_meta, text);
    for (int col = 0; col < 4; ++col) {
        set_text(s_stems[col], bazi_stem_text(s_chart.pillar[col].stem));
        set_text(s_branches[col], bazi_branch_text(s_chart.pillar[col].branch));
        ui_pixel_text_color(s_stems[col],
            element_color(bazi_element_of_stem(s_chart.pillar[col].stem)));
        ui_pixel_text_color(s_branches[col],
            element_color(bazi_element_of_branch(s_chart.pillar[col].branch)));
    }
    initialize_chart_position();
    refresh_luck_table();
    lv_screen_load(s_screen);
}

const char *field_name(Field field) {
    static const char *const names[] = {
        "性别", "历法", "出生年", "出生月", "出生日", "出生时", "出生分", "闰月",
        "出生地", "子时规则", "时间制", "开始排盘",
    };
    return field < Field::Count ? names[(uint8_t)field] : "";
}

void field_value(Field field, char *out, size_t capacity) {
    switch (field) {
        case Field::Sex:
            std::snprintf(out, capacity, s_birth.sex == BAZI_SEX_FEMALE ? "女" : "男");
            break;
        case Field::Calendar:
            std::snprintf(out, capacity, s_birth.calendar == BAZI_CALENDAR_LUNAR ? "农历" : "公历");
            break;
        case Field::Year: std::snprintf(out, capacity, "%d", s_birth.year); break;
        case Field::Month: std::snprintf(out, capacity, "%u", s_birth.month); break;
        case Field::Day: std::snprintf(out, capacity, "%u", s_birth.day); break;
        case Field::Hour: std::snprintf(out, capacity, "%02u", s_birth.hour); break;
        case Field::Minute: std::snprintf(out, capacity, "%02u", s_birth.minute); break;
        case Field::Leap:
            std::snprintf(out, capacity, s_birth.calendar == BAZI_CALENDAR_LUNAR
                ? (s_birth.lunar_leap ? "是" : "否") : "仅农历");
            break;
        case Field::City: std::snprintf(out, capacity, "%s", bazi_city_name(s_birth.city_id)); break;
        case Field::ZiRule:
            std::snprintf(out, capacity, s_birth.zi_rule == BAZI_ZI_LATE ? "晚子时" : "早子时");
            break;
        case Field::TimeMode:
            std::snprintf(out, capacity, s_birth.time_mode == BAZI_TIME_TRUE_SOLAR
                ? "真太阳时" : "标准时");
            break;
        case Field::Calculate: std::snprintf(out, capacity, "排盘"); break;
        default: out[0] = '\0'; break;
    }
}

void refresh_editor() {
    if (!s_screen) return;
    if (s_editor_message[0]) set_text(s_editor_header, s_editor_message);
    else set_text(s_editor_header, s_editor_editing
        ? "上下调整 · 确定完成" : "上下选择 · 确定修改");
    const uint8_t first = s_editor_selected >= kEditorRows
        ? (uint8_t)(s_editor_selected - kEditorRows + 1) : 0;
    for (uint8_t row = 0; row < kEditorRows; ++row) {
        const uint8_t index = first + row;
        const bool visible = index < (uint8_t)Field::Count;
        lv_obj_set_flag(s_editor_rows[row], LV_OBJ_FLAG_HIDDEN, !visible);
        if (!visible) continue;
        const Field field = (Field)index;
        char value[32];
        field_value(field, value, sizeof(value));
        char display[40];
        const bool selected = index == s_editor_selected;
        const bool scroll_field = field == Field::Sex || field == Field::Calendar ||
            field == Field::Leap || field == Field::City || field == Field::ZiRule ||
            field == Field::TimeMode;
        if (selected && s_editor_editing && scroll_field)
            std::snprintf(display, sizeof(display), "< %s >", value);
        else
            std::snprintf(display, sizeof(display), "%s", value);
        set_text(s_editor_names[row], field_name(field));
        set_text(s_editor_values[row], display);
        ui_pixel_bg_color(s_editor_rows[row],
            selected ? (s_editor_editing ? UI_WARNING : UI_ACCENT) : UI_BG);
        ui_pixel_text_color(s_editor_names[row], selected ? UI_ON_ACCENT : UI_TEXT);
        ui_pixel_text_color(s_editor_values[row], selected ? UI_ON_ACCENT : UI_TEXT_DIM);
    }
    set_text(s_footer_primary, s_editor_editing
        ? "上下调整 · 确定完成" : "上下选择 · 确定修改");
}

void build_editor() {
    s_mode = Mode::Editor;
    s_screen = lv_obj_create(nullptr);
    ui_pixel_background(s_screen);
    add_top_bar("出生资料");
    s_editor_header = label(s_screen, "", &buddy_font_16, UI_TEXT_DIM, 12, 29, 216, 19);
    for (uint8_t row = 0; row < kEditorRows; ++row) {
        lv_obj_t *item = lv_obj_create(s_screen);
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(item, 12, 50 + row * 25);
        lv_obj_set_size(item, 216, 23);
        lv_obj_set_style_radius(item, 0, 0);
        lv_obj_set_style_pad_all(item, 0, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        s_editor_rows[row] = item;
        s_editor_names[row] = label(item, "", &buddy_font_16, UI_TEXT, 6, 1, 112, 21);
        s_editor_values[row] = label(item, "", &buddy_font_16, UI_TEXT_DIM,
                                     112, 1, 98, 21, LV_TEXT_ALIGN_RIGHT);
    }
    s_footer_primary = ui_pixel_footer(
        s_screen, "上下选择 · 确定修改", "长按下键排盘", &buddy_font_16);
    s_editor_selected = 0;
    s_editor_editing = false;
    s_editor_message[0] = '\0';
    refresh_editor();
    lv_screen_load(s_screen);
}

void close_screen() {
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = nullptr;
    }
    s_battery = {};
    s_wifi_bars[0] = s_wifi_bars[1] = s_wifi_bars[2] = nullptr;
    s_date = s_meta = s_method = nullptr;
    for (int i = 0; i < 4; ++i) {
        s_stems[i] = s_branches[i] = nullptr;
    }
    for (auto &c : s_luck_cells) c = {};
    for (auto &row : s_flow_cells) for (auto &c : row) c = {};
    s_luck_header_left = s_luck_header_right = nullptr;
    s_editor_header = nullptr;
    for (int i = 0; i < kEditorRows; ++i) {
        s_editor_rows[i] = s_editor_names[i] = s_editor_values[i] = nullptr;
    }
    s_footer_primary = nullptr;
}

void open_editor() {
    close_screen();
    s_editor_message[0] = '\0';
    build_editor();
}

void normalize_birth_day() {
    uint8_t max_day = bazi_birth_max_day(&s_birth);
    if (!max_day && s_birth.calendar == BAZI_CALENDAR_LUNAR && s_birth.lunar_leap) {
        s_birth.lunar_leap = 0;
        max_day = bazi_birth_max_day(&s_birth);
    }
    if (max_day && s_birth.day > max_day) s_birth.day = max_day;
}

void adjust_field(int delta) {
    const Field field = (Field)s_editor_selected;
    switch (field) {
        case Field::Sex:
            s_birth.sex = delta > 0 ? BAZI_SEX_FEMALE : BAZI_SEX_MALE;
            break;
        case Field::Calendar:
            s_birth.calendar = s_birth.calendar == BAZI_CALENDAR_LUNAR
                ? BAZI_CALENDAR_SOLAR : BAZI_CALENDAR_LUNAR;
            if (s_birth.calendar == BAZI_CALENDAR_LUNAR) s_birth.lunar_leap = 0;
            normalize_birth_day();
            break;
        case Field::Year: {
            int year = s_birth.year + delta;
            if (year < 1900) year = 1900;
            if (year > 2100) year = 2100;
            s_birth.year = (int16_t)year;
            normalize_birth_day();
            break;
        }
        case Field::Month: {
            int month = (int)s_birth.month + delta;
            if (month < 1) month = 12;
            if (month > 12) month = 1;
            s_birth.month = (uint8_t)month;
            normalize_birth_day();
            break;
        }
        case Field::Day: {
            const uint8_t max_day = bazi_birth_max_day(&s_birth);
            if (!max_day) break;
            int day = (int)s_birth.day + delta;
            if (day < 1) day = max_day;
            if (day > max_day) day = 1;
            s_birth.day = (uint8_t)day;
            break;
        }
        case Field::Hour:
            s_birth.hour = (uint8_t)((s_birth.hour + 24 + delta) % 24);
            break;
        case Field::Minute: {
            s_birth.minute = bazi_minute_step(s_birth.minute, delta);
            break;
        }
        case Field::Leap:
            if (s_birth.calendar == BAZI_CALENDAR_LUNAR) {
                bazi_birth_t next = s_birth;
                next.lunar_leap = !next.lunar_leap;
                if (bazi_birth_max_day(&next)) {
                    s_birth = next;
                    normalize_birth_day();
                }
            }
            break;
        case Field::City: {
            int city = (int)s_birth.city_id + delta;
            if (city < 0) city = bazi_city_count() - 1;
            if (city >= bazi_city_count()) city = 0;
            s_birth.city_id = (uint8_t)city;
            s_birth.longitude_e4 = bazi_city_longitude_e4(s_birth.city_id);
            break;
        }
        case Field::ZiRule:
            s_birth.zi_rule = s_birth.zi_rule == BAZI_ZI_LATE ? BAZI_ZI_EARLY : BAZI_ZI_LATE;
            break;
        case Field::TimeMode:
            s_birth.time_mode = s_birth.time_mode == BAZI_TIME_TRUE_SOLAR
                ? BAZI_TIME_STANDARD : BAZI_TIME_TRUE_SOLAR;
            break;
        default:
            break;
    }
}

void move_editor(int delta) {
    int index = (int)s_editor_selected + delta;
    if (index < 0) index = (int)Field::Count - 1;
    if (index >= (int)Field::Count) index = 0;
    s_editor_selected = (uint8_t)index;
    s_editor_editing = false;
}

void calculate_editor() {
    if (!calculate_chart()) {
        refresh_editor();
        return;
    }
    close_screen();
    s_flow_row = 0;
    build_chart();
}

void chart_key(uint8_t btn, app_btn_event_t event) {
    if (event != BTN_EVT_CLICK && !(btn == BSP_BTN_DOWN && event == BTN_EVT_LONG)) return;
    if (btn == BSP_BTN_DOWN && event == BTN_EVT_LONG) {
        open_editor();
        return;
    }
    if (event != BTN_EVT_CLICK) return;
    if (btn == BSP_BTN_DOWN && s_flow_row + kVisibleRows < BAZI_YEARS_PER_LUCK) {
        s_flow_row = (uint8_t)(s_flow_row + kVisibleRows);
    } else if (btn == BSP_BTN_UP && s_flow_row > 0) {
        s_flow_row = s_flow_row >= kVisibleRows ? s_flow_row - kVisibleRows : 0;
    } else if (btn == BSP_BTN_OK) {
        s_flow_row = (uint8_t)((s_current_flow_row / kVisibleRows) * kVisibleRows);
    }
    refresh_luck_table();
}

void editor_key(uint8_t btn, app_btn_event_t event) {
    if (event == BTN_EVT_LONG && btn == BSP_BTN_DOWN) {
        calculate_editor();
        return;
    }
    if (event != BTN_EVT_CLICK) return;
    s_editor_message[0] = '\0';
    if (s_editor_editing) {
        if (btn == BSP_BTN_UP) adjust_field(1);
        else if (btn == BSP_BTN_DOWN) adjust_field(-1);
        else if (btn == BSP_BTN_OK) {
            s_editor_editing = false;
            move_editor(1);
        }
    } else if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        move_editor(btn == BSP_BTN_UP ? -1 : 1);
    } else if (btn == BSP_BTN_OK) {
        const Field field = (Field)s_editor_selected;
        if (field == Field::Calculate) {
            calculate_editor();
            return;
        }
        else s_editor_editing = true;
    }
    if (s_screen) refresh_editor();
}

void start() {
    if (!bsp_lvgl_lock(1000)) return;
    if (s_screen) {
        bsp_lvgl_unlock();
        return;
    }
    if (!s_form_initialized) {
        default_birth(&s_birth);
        s_form_initialized = true;
    }
    s_has_chart = false;
    open_editor();
    ESP_LOGI(kTag, "started in form; free=%u", (unsigned)esp_get_free_heap_size());
    bsp_lvgl_unlock();
}

void stop() {
    if (!bsp_lvgl_lock(-1)) return;
    close_screen();
    s_mode = Mode::Chart;
    s_has_chart = false;
    s_editor_editing = false;
    ESP_LOGI(kTag, "stopped; free=%u", (unsigned)esp_get_free_heap_size());
    bsp_lvgl_unlock();
}

void on_key(uint8_t btn, app_btn_event_t event, uint16_t) {
    if (!bsp_lvgl_lock(500)) return;
    if (s_mode == Mode::Editor) editor_key(btn, event);
    else chart_key(btn, event);
    bsp_lvgl_unlock();
}

bool on_back() {
    if (!bsp_lvgl_lock(500)) return true;
    bool handled = false;
    if (s_mode == Mode::Chart) {
        close_screen();
        build_editor();
        handled = true;
    }
    bsp_lvgl_unlock();
    return handled;
}

bool prepare_stop() {
    return true;
}

} // namespace
} // namespace bazi

extern const passport_app_t g_bazi_app = {
    .id = APP_ID_BAZI,
    .name = "八字排盘",
    .en_name = "BAZI",
    .desc = "四柱命盘\n大运流年",
    .tag = "本地历法",
    .theme_color = 0xDDB36C,
    .init = nullptr,
    .start = bazi::start,
    .stop = bazi::stop,
    .on_key = bazi::on_key,
    .prepare_stop = bazi::prepare_stop,
    .on_back = bazi::on_back,
};
