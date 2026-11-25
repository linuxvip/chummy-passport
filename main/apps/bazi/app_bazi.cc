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
constexpr uint8_t kVisibleLuck = 5;
constexpr int kColumnW = 43;
constexpr uint8_t kVisibleRows = 5;
constexpr int kFlowTop = 185;
constexpr int kFlowStep = 26;
constexpr int kFlowHeight = 25;
constexpr uint8_t kEditorRows = 8;
constexpr uint8_t kMaxChips = 3;

enum class Mode : uint8_t { Chart, Editor };
enum class Field : uint8_t {
    Sex, Calendar, Date, Time, ZiRule, City, TimeMode, Calculate, Count
};
static_assert((uint8_t)Field::Count == kEditorRows,
              "editor rows must match fields");

struct Cell {
    lv_obj_t *box = nullptr;
    lv_obj_t *a = nullptr;
    lv_obj_t *b = nullptr;
};

struct EditorChip {
    lv_obj_t *box = nullptr;
    lv_obj_t *label = nullptr;
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
lv_obj_t *s_luck_marks[kVisibleLuck] = {};
Cell s_flow_cells[kVisibleRows][kVisibleLuck] = {};
lv_obj_t *s_flow_dots[2] = {};
lv_obj_t *s_editor_rows[kEditorRows] = {};
lv_obj_t *s_editor_names[kEditorRows] = {};
lv_obj_t *s_editor_values[kEditorRows] = {};
lv_obj_t *s_editor_bars[kEditorRows] = {};
EditorChip s_chips[kEditorRows][kMaxChips] = {};
uint8_t s_chip_count[kEditorRows] = {};
lv_obj_t *s_date_parts[3] = {};
lv_obj_t *s_time_parts[2] = {};
lv_obj_t *s_date_mark = nullptr;
lv_obj_t *s_time_mark = nullptr;
lv_obj_t *s_city_arrows[2] = {};
lv_obj_t *s_footer_primary = nullptr;
uint8_t s_flow_row = 0;
uint8_t s_editor_selected = 0;
uint8_t s_editor_segment = 0;
bool s_editor_editing = false;
char s_editor_message[64] = {};
uint8_t s_current_luck = 0;
uint8_t s_current_flow_row = 0;
uint8_t s_luck_first = 0;
uint8_t s_selected_luck = 0;

const char *const kMonthNames[] = {
    "", "正", "二", "三", "四", "五", "六",
    "七", "八", "九", "十", "十一", "十二",
};
const char *const kDayNames[] = {
    "", "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十",
    "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十",
    "廿一", "廿二", "廿三", "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十",
};

constexpr int kEditorRowTop[kEditorRows] = {
    42, 66, 106, 130, 154, 194, 218, 243,
};
constexpr int kDatePartX[3] = {118, 162, 190};
constexpr int kDatePartW[3] = {36, 20, 20};
constexpr int kTimePartX[2] = {146, 182};
constexpr int kTimePartW[2] = {28, 28};

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

void reset_luck_window() {
    s_selected_luck = s_current_luck;
    s_luck_first = s_current_luck > 2 ? s_current_luck - 2 : 0;
    if (s_luck_first + kVisibleLuck > BAZI_LUCK_COUNT)
        s_luck_first = BAZI_LUCK_COUNT - kVisibleLuck;
}

void initialize_chart_position() {
    int year = 2026, month = 1, day = 1;
    local_date(&year, &month, &day);
    s_current_luck = bazi_current_luck_index(&s_chart, year);
    s_current_flow_row = bazi_flow_row_for_year(&s_chart, s_current_luck, year);
    reset_luck_window();
    s_flow_row = (uint8_t)((s_current_flow_row / kVisibleRows) * kVisibleRows);
    if (s_flow_row > BAZI_YEARS_PER_LUCK - kVisibleRows)
        s_flow_row = BAZI_YEARS_PER_LUCK - kVisibleRows;
}

void refresh_luck_table() {
    if (!s_screen || !s_has_chart) return;
    char text[48];
    std::snprintf(text, sizeof(text), "大运 %s %d %u-%u",
                  s_chart.luck.forward ? "顺排" : "逆排",
                  s_chart.luck.step[s_luck_first].start_year,
                  (unsigned)(s_luck_first + 1),
                  (unsigned)(s_luck_first + kVisibleLuck));
    set_text(s_luck_header_left, text);
    std::snprintf(text, sizeof(text), "流年 第%u-%u年",
                  (unsigned)(s_flow_row + 1),
                  (unsigned)(s_flow_row + kVisibleRows));
    set_text(s_luck_header_right, text);
    const uint8_t page = s_flow_row >= kVisibleRows ? 1 : 0;
    for (int dot = 0; dot < 2; ++dot) {
        const bool active = dot == page;
        ui_pixel_bg_color(s_flow_dots[dot], active ? UI_ACCENT : UI_BG);
        lv_obj_set_style_border_color(s_flow_dots[dot],
            lv_color_hex(active ? UI_ACCENT : UI_BORDER), 0);
    }

    for (uint8_t col = 0; col < kVisibleLuck; ++col) {
        Cell &target = s_luck_cells[col];
        const uint8_t index = (uint8_t)(s_luck_first + col);
        const bool valid = index < BAZI_LUCK_COUNT;
        const bool selected = valid && index == s_selected_luck;
        set_text(target.a, valid ? bazi_stem_text(s_chart.luck.step[index].ganzhi.stem) : "");
        set_text(target.b, valid ? bazi_branch_text(s_chart.luck.step[index].ganzhi.branch) : "");
        ui_pixel_text_color(target.a, valid ? element_color(
            bazi_element_of_stem(s_chart.luck.step[index].ganzhi.stem)) : UI_TEXT_DIM);
        ui_pixel_text_color(target.b, valid ? element_color(
            bazi_element_of_branch(s_chart.luck.step[index].ganzhi.branch)) : UI_TEXT_DIM);
        ui_pixel_bg_color(target.box, selected ? UI_SURFACE_ALT : UI_SURFACE);
        lv_obj_set_style_border_color(target.box,
            lv_color_hex(selected ? UI_ACCENT : UI_BORDER), 0);
        lv_obj_set_flag(s_luck_marks[col], LV_OBJ_FLAG_HIDDEN, !selected);
    }

    for (uint8_t row = 0; row < kVisibleRows; ++row) {
        const uint8_t flow_index = (uint8_t)(s_flow_row + row);
        for (uint8_t col = 0; col < kVisibleLuck; ++col) {
            Cell &target = s_flow_cells[row][col];
            const uint8_t luck_index = (uint8_t)(s_luck_first + col);
            if (luck_index >= BAZI_LUCK_COUNT || flow_index >= BAZI_YEARS_PER_LUCK) {
                set_text(target.a, "");
                ui_pixel_bg_color(target.box, UI_SURFACE);
                lv_obj_set_style_border_color(target.box,
                    lv_color_hex(UI_BORDER), 0);
                continue;
            }
            const int year = s_chart.luck.step[luck_index].start_year + flow_index;
            const bazi_ganzhi_t gz = bazi_flow_year(year);
            char flow[7];
            ganzhi_text(gz, flow);
            set_text(target.a, flow);
            ui_pixel_text_color(target.a, UI_TEXT);
            const bool selected_col = luck_index == s_selected_luck;
            const bool current = selected_col && luck_index == s_current_luck &&
                                 flow_index == s_current_flow_row;
            ui_pixel_bg_color(target.box, selected_col ? UI_SURFACE_ALT : UI_SURFACE);
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
                               12, 132, 120, 14);
    s_luck_header_right = label(s_screen, "", &ganzhi_font_12, UI_TEXT_DIM,
                                132, 132, 84, 14, LV_TEXT_ALIGN_RIGHT);
    for (int dot = 0; dot < 2; ++dot) {
        lv_obj_t *box = lv_obj_create(s_screen);
        lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(box, 219 + dot * 6, 138);
        lv_obj_set_size(box, 3, 3);
        lv_obj_set_style_radius(box, 0, 0);
        lv_obj_set_style_pad_all(box, 0, 0);
        lv_obj_set_style_border_width(box, 1, 0);
        lv_obj_set_style_border_color(box, lv_color_hex(UI_BORDER), 0);
        lv_obj_set_style_bg_color(box, lv_color_hex(UI_BG), 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        s_flow_dots[dot] = box;
    }
    for (uint8_t col = 0; col < kVisibleLuck; ++col) {
        Cell &target = s_luck_cells[col];
        target.box = cell(s_screen, 12 + col * kColumnW, 147, kColumnW, 38);
        target.a = ui_pixel_label(target.box, "", &ganzhi_font_20, UI_TEXT);
        target.b = ui_pixel_label(target.box, "", &ganzhi_font_20, UI_TEXT);
        lv_obj_set_style_text_align(target.a, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_align(target.b, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(target.a, 0, 8);
        lv_obj_set_size(target.a, 20, 21);
        lv_obj_set_pos(target.b, 21, 8);
        lv_obj_set_size(target.b, 20, 21);
        lv_obj_t *mark = lv_obj_create(target.box);
        lv_obj_remove_flag(mark, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(mark, 0, 0);
        lv_obj_set_size(mark, kColumnW - 2, 2);
        lv_obj_set_style_radius(mark, 0, 0);
        lv_obj_set_style_border_width(mark, 0, 0);
        lv_obj_set_style_bg_color(mark, lv_color_hex(UI_ACCENT), 0);
        lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
        lv_obj_add_flag(mark, LV_OBJ_FLAG_HIDDEN);
        s_luck_marks[col] = mark;
    }
    for (uint8_t row = 0; row < kVisibleRows; ++row) {
        for (uint8_t col = 0; col < kVisibleLuck; ++col) {
            Cell &target = s_flow_cells[row][col];
            target.box = cell(s_screen, 12 + col * kColumnW,
                              kFlowTop + row * kFlowStep, kColumnW, kFlowHeight);
            target.a = ui_pixel_label(target.box, "", &ganzhi_font_13, UI_TEXT);
            lv_obj_center(target.a);
        }
    }

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

void section_header(const char *text, int y, int rule_x) {
    label(s_screen, text, &liuyao_font_12, UI_TEXT_DIM, 12, y, 90, 14);
    lv_obj_t *rule = lv_obj_create(s_screen);
    lv_obj_remove_flag(rule, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(rule, rule_x, y + 7);
    lv_obj_set_size(rule, 228 - rule_x, 1);
    lv_obj_set_style_radius(rule, 0, 0);
    lv_obj_set_style_border_width(rule, 0, 0);
    lv_obj_set_style_bg_color(rule, lv_color_hex(UI_BORDER), 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_60, 0);
}

lv_obj_t *editor_row(uint8_t index, const char *title) {
    const bool calculate = (Field)index == Field::Calculate;
    const int height = calculate ? 26 : 24;
    lv_obj_t *row = lv_obj_create(s_screen);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(row, 12, kEditorRowTop[index]);
    lv_obj_set_size(row, 216, height);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    s_editor_rows[index] = row;
    if (title) {
        s_editor_names[index] = label(row, title, &buddy_font_16, UI_TEXT_DIM,
                                      9, 2, 90, 20);
    }
    lv_obj_t *bar = lv_obj_create(row);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, 3, height);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(UI_ACCENT), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    s_editor_bars[index] = bar;
    return row;
}

void editor_chip(uint8_t row, uint8_t index, const char *text, int x, int width) {
    EditorChip &chip = s_chips[row][index];
    chip.box = lv_obj_create(s_editor_rows[row]);
    lv_obj_remove_flag(chip.box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(chip.box, x, 1);
    lv_obj_set_size(chip.box, width, 22);
    lv_obj_set_style_radius(chip.box, 0, 0);
    lv_obj_set_style_pad_all(chip.box, 0, 0);
    lv_obj_set_style_border_width(chip.box, 1, 0);
    lv_obj_set_style_bg_color(chip.box, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(chip.box, LV_OPA_COVER, 0);
    chip.label = label(chip.box, text, &buddy_font_16, UI_TEXT_DIM,
                       0, 0, width - 2, 20, LV_TEXT_ALIGN_CENTER);
    s_chip_count[row] = (uint8_t)(index + 1);
}

lv_obj_t *part_label(lv_obj_t *row, int x, int width) {
    return label(row, "", &buddy_font_16, UI_TEXT, x, 2, width, 20,
                 LV_TEXT_ALIGN_CENTER);
}

lv_obj_t *segment_mark(lv_obj_t *row) {
    lv_obj_t *mark = lv_obj_create(row);
    lv_obj_remove_flag(mark, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(mark, 0, 21);
    lv_obj_set_size(mark, 20, 2);
    lv_obj_set_style_radius(mark, 0, 0);
    lv_obj_set_style_border_width(mark, 0, 0);
    lv_obj_set_style_bg_color(mark, lv_color_hex(UI_ACCENT), 0);
    lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
    lv_obj_add_flag(mark, LV_OBJ_FLAG_HIDDEN);
    return mark;
}

void set_chip(uint8_t row, uint8_t index, bool active, bool disabled) {
    if (index >= s_chip_count[row]) return;
    EditorChip &chip = s_chips[row][index];
    ui_pixel_bg_color(chip.box, active ? UI_ACCENT : UI_BG);
    lv_obj_set_style_border_color(chip.box,
        lv_color_hex(active ? UI_ACCENT : UI_BORDER), 0);
    ui_pixel_text_color(chip.label, active ? UI_ON_ACCENT : UI_TEXT_DIM);
    lv_obj_set_style_text_opa(chip.label, disabled ? LV_OPA_50 : LV_OPA_COVER, 0);
}

bool lunar_leap_available() {
    bazi_birth_t probe = s_birth;
    probe.calendar = BAZI_CALENDAR_LUNAR;
    probe.lunar_leap = 1;
    return bazi_birth_max_day(&probe) != 0;
}

void refresh_editor() {
    if (!s_screen) return;
    const uint8_t selected = s_editor_selected;

    for (uint8_t row = 0; row < kEditorRows; ++row) {
        const bool focused = row == selected;
        if ((Field)row == Field::Calculate) {
            ui_pixel_bg_color(s_editor_rows[row],
                              focused ? UI_ACCENT : UI_SURFACE_ALT);
            ui_pixel_text_color(s_editor_values[row],
                                focused ? UI_ON_ACCENT : UI_ACCENT);
            continue;
        }
        ui_pixel_bg_color(s_editor_rows[row], focused ? UI_SURFACE_ALT : UI_BG);
        lv_obj_set_flag(s_editor_bars[row], LV_OBJ_FLAG_HIDDEN, !focused);
        if (s_editor_names[row])
            ui_pixel_text_color(s_editor_names[row],
                                focused ? UI_TEXT : UI_TEXT_DIM);
    }

    set_chip((uint8_t)Field::Sex, 0, s_birth.sex == BAZI_SEX_MALE, false);
    set_chip((uint8_t)Field::Sex, 1, s_birth.sex == BAZI_SEX_FEMALE, false);

    const bool lunar = s_birth.calendar == BAZI_CALENDAR_LUNAR;
    const bool leap_ok = lunar && lunar_leap_available();
    set_chip((uint8_t)Field::Calendar, 0, !lunar, false);
    set_chip((uint8_t)Field::Calendar, 1, lunar, false);
    set_chip((uint8_t)Field::Calendar, 2, lunar && s_birth.lunar_leap, !leap_ok);

    set_chip((uint8_t)Field::ZiRule, 0, s_birth.zi_rule == BAZI_ZI_EARLY, false);
    set_chip((uint8_t)Field::ZiRule, 1, s_birth.zi_rule == BAZI_ZI_LATE, false);
    set_chip((uint8_t)Field::TimeMode, 0,
             s_birth.time_mode == BAZI_TIME_STANDARD, false);
    set_chip((uint8_t)Field::TimeMode, 1,
             s_birth.time_mode == BAZI_TIME_TRUE_SOLAR, false);

    char text[8];
    std::snprintf(text, sizeof(text), "%04d", (int)s_birth.year);
    set_text(s_date_parts[0], text);
    std::snprintf(text, sizeof(text), "%02u", (unsigned)s_birth.month);
    set_text(s_date_parts[1], text);
    std::snprintf(text, sizeof(text), "%02u", (unsigned)s_birth.day);
    set_text(s_date_parts[2], text);
    const bool date_editing = selected == (uint8_t)Field::Date && s_editor_editing;
    for (uint8_t i = 0; i < 3; ++i) {
        ui_pixel_text_color(s_date_parts[i],
            date_editing && i == s_editor_segment ? UI_ACCENT : UI_TEXT);
    }
    lv_obj_set_flag(s_date_mark, LV_OBJ_FLAG_HIDDEN, !date_editing);
    if (date_editing) {
        lv_obj_set_pos(s_date_mark, kDatePartX[s_editor_segment], 21);
        lv_obj_set_size(s_date_mark, kDatePartW[s_editor_segment], 2);
    }

    std::snprintf(text, sizeof(text), "%02u", (unsigned)s_birth.hour);
    set_text(s_time_parts[0], text);
    std::snprintf(text, sizeof(text), "%02u", (unsigned)s_birth.minute);
    set_text(s_time_parts[1], text);
    const bool time_editing = selected == (uint8_t)Field::Time && s_editor_editing;
    for (uint8_t i = 0; i < 2; ++i) {
        ui_pixel_text_color(s_time_parts[i],
            time_editing && i == s_editor_segment ? UI_ACCENT : UI_TEXT);
    }
    lv_obj_set_flag(s_time_mark, LV_OBJ_FLAG_HIDDEN, !time_editing);
    if (time_editing) {
        lv_obj_set_pos(s_time_mark, kTimePartX[s_editor_segment], 21);
        lv_obj_set_size(s_time_mark, kTimePartW[s_editor_segment], 2);
    }

    set_text(s_editor_values[(uint8_t)Field::City],
             bazi_city_name(s_birth.city_id));
    const bool city_focused = selected == (uint8_t)Field::City;
    const bool city_editing = city_focused && s_editor_editing;
    ui_pixel_text_color(s_editor_values[(uint8_t)Field::City],
                        city_focused ? UI_ACCENT : UI_TEXT);
    for (lv_obj_t *arrow : s_city_arrows)
        ui_pixel_text_color(arrow, city_editing ? UI_ACCENT : UI_TEXT_DIM);

    uint32_t footer_color = UI_TEXT_DIM;
    const char *hint = "上下选择 · 确定切换";
    if (s_editor_editing) {
        hint = selected == (uint8_t)Field::City
            ? "上下调整 · 确定完成" : "上下调整 · 确定下一段";
    } else if (selected == (uint8_t)Field::Calculate) {
        hint = "确定排盘";
    }
    if (s_editor_message[0]) {
        set_text(s_footer_primary, s_editor_message);
        footer_color = UI_DANGER;
    } else {
        set_text(s_footer_primary, hint);
    }
    ui_pixel_text_color(s_footer_primary, footer_color);
}

void build_editor() {
    s_mode = Mode::Editor;
    s_screen = lv_obj_create(nullptr);
    ui_pixel_background(s_screen);
    add_top_bar("出生资料");

    section_header("基本", 28, 44);

    lv_obj_t *row = editor_row((uint8_t)Field::Sex, "性别");
    editor_chip((uint8_t)Field::Sex, 0, "男", 146, 30);
    editor_chip((uint8_t)Field::Sex, 1, "女", 180, 30);

    editor_row((uint8_t)Field::Calendar, "历法");
    editor_chip((uint8_t)Field::Calendar, 0, "公历", 70, 44);
    editor_chip((uint8_t)Field::Calendar, 1, "农历", 118, 44);
    editor_chip((uint8_t)Field::Calendar, 2, "闰月", 166, 44);

    section_header("出生时刻", 92, 68);

    row = editor_row((uint8_t)Field::Date, "出生日期");
    s_date_parts[0] = part_label(row, 118, 36);
    label(row, "-", &buddy_font_16, UI_TEXT_DIM, 154, 2, 8, 20,
          LV_TEXT_ALIGN_CENTER);
    s_date_parts[1] = part_label(row, 162, 20);
    label(row, "-", &buddy_font_16, UI_TEXT_DIM, 182, 2, 8, 20,
          LV_TEXT_ALIGN_CENTER);
    s_date_parts[2] = part_label(row, 190, 20);
    s_date_mark = segment_mark(row);

    row = editor_row((uint8_t)Field::Time, "出生时间");
    s_time_parts[0] = part_label(row, 146, 28);
    label(row, ":", &buddy_font_16, UI_TEXT_DIM, 174, 2, 8, 20,
          LV_TEXT_ALIGN_CENTER);
    s_time_parts[1] = part_label(row, 182, 28);
    s_time_mark = segment_mark(row);

    editor_row((uint8_t)Field::ZiRule, "子时");
    editor_chip((uint8_t)Field::ZiRule, 0, "早子时", 90, 58);
    editor_chip((uint8_t)Field::ZiRule, 1, "晚子时", 152, 58);

    section_header("地点与校正", 180, 80);

    row = editor_row((uint8_t)Field::City, "出生地");
    s_city_arrows[0] = label(row, "<", &buddy_font_16, UI_TEXT_DIM,
                             114, 2, 12, 20, LV_TEXT_ALIGN_CENTER);
    s_editor_values[(uint8_t)Field::City] = label(
        row, "", &buddy_font_16, UI_TEXT, 126, 2, 72, 20, LV_TEXT_ALIGN_CENTER);
    s_city_arrows[1] = label(row, ">", &buddy_font_16, UI_TEXT_DIM,
                             200, 2, 12, 20, LV_TEXT_ALIGN_CENTER);

    editor_row((uint8_t)Field::TimeMode, "时间制");
    editor_chip((uint8_t)Field::TimeMode, 0, "标准时", 74, 58);
    editor_chip((uint8_t)Field::TimeMode, 1, "真太阳时", 136, 74);

    row = editor_row((uint8_t)Field::Calculate, nullptr);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_SURFACE_ALT), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(UI_ACCENT), 0);
    s_editor_values[(uint8_t)Field::Calculate] = label(
        row, "开始排盘", &buddy_font_16, UI_ACCENT, 0, 3, 214, 20,
        LV_TEXT_ALIGN_CENTER);

    s_footer_primary = ui_pixel_footer(
        s_screen, "上下选择 · 确定切换", "长按下键直接排盘", &buddy_font_16);
    s_editor_selected = 0;
    s_editor_segment = 0;
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
    for (auto &mark : s_luck_marks) mark = nullptr;
    for (auto &row : s_flow_cells) for (auto &c : row) c = {};
    s_luck_header_left = s_luck_header_right = nullptr;
    s_flow_dots[0] = s_flow_dots[1] = nullptr;
    for (int i = 0; i < kEditorRows; ++i) {
        s_editor_rows[i] = nullptr;
        s_editor_names[i] = nullptr;
        s_editor_values[i] = nullptr;
        s_editor_bars[i] = nullptr;
        s_chip_count[i] = 0;
        for (int c = 0; c < kMaxChips; ++c) s_chips[i][c] = {};
    }
    for (auto &part : s_date_parts) part = nullptr;
    for (auto &part : s_time_parts) part = nullptr;
    s_date_mark = s_time_mark = nullptr;
    s_city_arrows[0] = s_city_arrows[1] = nullptr;
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

void cycle_calendar() {
    if (s_birth.calendar == BAZI_CALENDAR_SOLAR) {
        s_birth.calendar = BAZI_CALENDAR_LUNAR;
        s_birth.lunar_leap = 0;
    } else if (!s_birth.lunar_leap && lunar_leap_available()) {
        s_birth.lunar_leap = 1;
    } else {
        s_birth.calendar = BAZI_CALENDAR_SOLAR;
        s_birth.lunar_leap = 0;
    }
    normalize_birth_day();
}

void adjust_year(int delta) {
    int year = s_birth.year + delta;
    if (year < 1900) year = 1900;
    if (year > 2100) year = 2100;
    s_birth.year = (int16_t)year;
    normalize_birth_day();
}

void adjust_month(int delta) {
    int month = (int)s_birth.month + delta;
    if (month < 1) month = 12;
    if (month > 12) month = 1;
    s_birth.month = (uint8_t)month;
    normalize_birth_day();
}

void adjust_day(int delta) {
    const uint8_t max_day = bazi_birth_max_day(&s_birth);
    if (!max_day) return;
    int day = (int)s_birth.day + delta;
    if (day < 1) day = max_day;
    if (day > max_day) day = 1;
    s_birth.day = (uint8_t)day;
}

void adjust_time(int delta) {
    if (s_editor_segment == 0)
        s_birth.hour = (uint8_t)((s_birth.hour + 24 + delta) % 24);
    else
        s_birth.minute = bazi_minute_step(s_birth.minute, delta);
}

void adjust_city(int delta) {
    int city = (int)s_birth.city_id + delta;
    if (city < 0) city = bazi_city_count() - 1;
    if (city >= bazi_city_count()) city = 0;
    s_birth.city_id = (uint8_t)city;
    s_birth.longitude_e4 = bazi_city_longitude_e4(s_birth.city_id);
}

void adjust_active_field(int delta) {
    const Field field = (Field)s_editor_selected;
    if (field == Field::Date) {
        if (s_editor_segment == 0) adjust_year(delta);
        else if (s_editor_segment == 1) adjust_month(delta);
        else adjust_day(delta);
    } else if (field == Field::Time) {
        adjust_time(delta);
    } else if (field == Field::City) {
        adjust_city(delta);
    }
}

void move_editor(int delta) {
    int index = (int)s_editor_selected + delta;
    if (index < 0) index = (int)Field::Count - 1;
    if (index >= (int)Field::Count) index = 0;
    s_editor_selected = (uint8_t)index;
    s_editor_editing = false;
    s_editor_segment = 0;
}

void advance_editor_segment() {
    const Field field = (Field)s_editor_selected;
    if (field == Field::Date && s_editor_segment < 2) {
        ++s_editor_segment;
        return;
    }
    if (field == Field::Time && s_editor_segment < 1) {
        ++s_editor_segment;
        return;
    }
    s_editor_editing = false;
    s_editor_segment = 0;
    move_editor(1);
}

void activate_editor_field(Field field) {
    switch (field) {
        case Field::Sex:
            s_birth.sex = s_birth.sex == BAZI_SEX_FEMALE
                ? BAZI_SEX_MALE : BAZI_SEX_FEMALE;
            break;
        case Field::Calendar:
            cycle_calendar();
            break;
        case Field::Date:
        case Field::Time:
        case Field::City:
            s_editor_editing = true;
            s_editor_segment = 0;
            break;
        case Field::ZiRule:
            s_birth.zi_rule = s_birth.zi_rule == BAZI_ZI_LATE
                ? BAZI_ZI_EARLY : BAZI_ZI_LATE;
            break;
        case Field::TimeMode:
            s_birth.time_mode = s_birth.time_mode == BAZI_TIME_TRUE_SOLAR
                ? BAZI_TIME_STANDARD : BAZI_TIME_TRUE_SOLAR;
            break;
        default:
            break;
    }
}

void calculate_editor() {
    if (!calculate_chart()) {
        refresh_editor();
        return;
    }
    close_screen();
    build_chart();
}

void chart_key(uint8_t btn, app_btn_event_t event) {
    if (event == BTN_EVT_LONG && (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
        if (btn == BSP_BTN_UP && s_selected_luck > 0) {
            --s_selected_luck;
        } else if (btn == BSP_BTN_DOWN && s_selected_luck + 1 < BAZI_LUCK_COUNT) {
            ++s_selected_luck;
        }
        if (s_selected_luck < s_luck_first) {
            s_luck_first = s_selected_luck;
        } else if (s_selected_luck >= s_luck_first + kVisibleLuck) {
            s_luck_first = (uint8_t)(s_selected_luck - kVisibleLuck + 1);
        }
        refresh_luck_table();
        return;
    }
    if (event != BTN_EVT_CLICK) return;
    if (btn == BSP_BTN_DOWN && s_flow_row + kVisibleRows < BAZI_YEARS_PER_LUCK) {
        s_flow_row = (uint8_t)(s_flow_row + kVisibleRows);
    } else if (btn == BSP_BTN_UP && s_flow_row > 0) {
        s_flow_row = s_flow_row >= kVisibleRows ? s_flow_row - kVisibleRows : 0;
    } else if (btn == BSP_BTN_OK) {
        reset_luck_window();
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
        if (btn == BSP_BTN_UP) adjust_active_field(1);
        else if (btn == BSP_BTN_DOWN) adjust_active_field(-1);
        else if (btn == BSP_BTN_OK) advance_editor_segment();
    } else if (btn == BSP_BTN_UP) {
        move_editor(-1);
    } else if (btn == BSP_BTN_DOWN) {
        move_editor(1);
    } else if (btn == BSP_BTN_OK) {
        const Field field = (Field)s_editor_selected;
        if (field == Field::Calculate) {
            calculate_editor();
            return;
        }
        activate_editor_field(field);
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
