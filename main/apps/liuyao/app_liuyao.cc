#include "apps/app_base.h"
#include "apps/bazi/bazi_core.h"
#include "apps/home/home_profile.h"
#include "apps/liuyao/liuyao_core.h"
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

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace liuyao {
namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 240;
constexpr uint8_t kEditorRows = 6;
constexpr char kTag[] = "liuyao";

int screen_width(void) {
    return lv_display_get_horizontal_resolution(lv_display_get_default());
}

int screen_height(void) {
    return lv_display_get_vertical_resolution(lv_display_get_default());
}

void set_landscape(bool landscape) {
    const lv_display_rotation_t wanted =
        landscape ? LV_DISPLAY_ROTATION_90 : LV_DISPLAY_ROTATION_0;
    lv_display_t *display = lv_display_get_default();
    if (display && lv_display_get_rotation(display) != wanted)
        lv_display_set_rotation(display, wanted);
}

enum class Mode : uint8_t {
    Method,
    Editor,
    Preview,
    Chart,
    Meaning,
};

enum class Field : uint8_t {
    Digits,
    Year,
    Month,
    Day,
    Hour,
    Minute,
    TimeMode,
    City,
    Cast,
    Count,
};
constexpr uint8_t kFieldCount = static_cast<uint8_t>(Field::Count);

Mode s_mode = Mode::Method;
liuyao_input_t s_input{};
liuyao_chart_t s_chart{};
char s_city_name[HOME_CITY_MAX] = "北京";
bool s_city_from_home = true;
bool s_form_initialized = false;
bool s_editor_editing = false;
bool s_digit_editing = false;
uint8_t s_method_selected = 0;
uint8_t s_field_index = 0;
uint8_t s_digit_cursor = 0;
uint8_t s_first_visible = 0;
char s_editor_message[64] = {};

lv_obj_t *s_screen = nullptr;
lv_obj_t *s_rows[kEditorRows] = {};
lv_obj_t *s_row_names[kEditorRows] = {};
lv_obj_t *s_row_values[kEditorRows] = {};
lv_obj_t *s_method_rows[2] = {};
lv_obj_t *s_preview_lines[9] = {};
lv_obj_t *s_footer_primary = nullptr;
lv_obj_t *s_footer_secondary = nullptr;
lv_obj_t *s_band_right = nullptr;

ui_pixel_battery_t s_battery{};
lv_obj_t *s_wifi_bars[3] = {};

const char *const kSpiritShort[] = {
    "龙", "雀", "勾", "蛇", "虎", "玄",
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

lv_obj_t *block(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *obj = block(parent, x, y, w, h, UI_SURFACE);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(UI_BORDER), 0);
    return obj;
}

uint32_t element_color(uint8_t element) {
    return ui_theme_element_color(static_cast<ui_element_t>(element < 5 ? element : 0));
}

const char *marker_text(const liuyao_line_t &line) {
    if (!line.moving) return "";
    return line.yang ? "O →" : "X →";
}

bool local_time(int *year, int *month, int *day, int *hour, int *minute) {
    time_t now = time(nullptr);
    if (now < 1700000000) return false;
    now += time_sync_tz_offset_sec();
    struct tm tm {};
    gmtime_r(&now, &tm);
    *year = tm.tm_year + 1900;
    *month = tm.tm_mon + 1;
    *day = tm.tm_mday;
    *hour = tm.tm_hour;
    *minute = tm.tm_min;
    return true;
}

void initialize_location(void) {
    home_profile_t profile{};
    home_profile_get(&profile);
    const bool valid = home_profile_coordinates_valid(profile.lat, profile.lon);
    if (valid) {
        char *end = nullptr;
        const double lon = std::strtod(profile.lon, &end);
        if (end && end != profile.lon && lon >= -180.0 && lon <= 180.0) {
            s_input.longitude_e4 = static_cast<int32_t>(std::lround(lon * 10000.0));
            std::snprintf(s_city_name, sizeof(s_city_name), "%s",
                          profile.city[0] ? profile.city : bazi_city_name(0));
            s_city_from_home = true;
            return;
        }
    }
    s_input.city_id = 0;
    s_input.longitude_e4 = bazi_city_longitude_e4(0);
    std::snprintf(s_city_name, sizeof(s_city_name), "%s", bazi_city_name(0));
    s_city_from_home = true;
}

void initialize_input(void) {
    std::memset(&s_input, 0, sizeof(s_input));
    int year = 2000, month = 1, day = 1, hour = 12, minute = 0;
    if (!local_time(&year, &month, &day, &hour, &minute)) {
        std::snprintf(s_editor_message, sizeof(s_editor_message),
                      "时间未同步，请先校对日期");
        year = 2000;
        month = 1;
        day = 1;
        hour = 12;
        minute = 0;
    }
    s_input.method = LIUYAO_METHOD_TIME;
    s_input.year = (int16_t)year;
    s_input.month = (uint8_t)month;
    s_input.day = (uint8_t)day;
    s_input.hour = (uint8_t)hour;
    s_input.minute = (uint8_t)minute;
    s_input.time_mode = BAZI_TIME_STANDARD;
    s_input.zi_rule = settings_get_late_zi() ? BAZI_ZI_LATE : BAZI_ZI_EARLY;
    s_input.city_id = 0;
    s_input.digit_count = 0;
    initialize_location();
}

void close_screen(void) {
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = nullptr;
    }
    std::memset(s_rows, 0, sizeof(s_rows));
    std::memset(s_row_names, 0, sizeof(s_row_names));
    std::memset(s_row_values, 0, sizeof(s_row_values));
    std::memset(s_method_rows, 0, sizeof(s_method_rows));
    std::memset(s_preview_lines, 0, sizeof(s_preview_lines));
    s_footer_primary = s_footer_secondary = nullptr;
    s_band_right = nullptr;
    s_battery = {};
    s_wifi_bars[0] = s_wifi_bars[1] = s_wifi_bars[2] = nullptr;
}

void update_status(void) {
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
    const int width = screen_width();
    const int wifi_x = width - 96;
    lv_obj_t *top = block(s_screen, 0, 0, width, 26, UI_SURFACE);
    lv_obj_set_style_border_width(top, 1, 0);
    lv_obj_set_style_border_side(top, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(top, lv_color_hex(UI_BORDER), 0);
    if (title && title[0]) {
        const bool compact = width <= 240;
        label(top, title, compact ? &liuyao_font_12 : &buddy_font_16,
              UI_TEXT, 12, compact ? 5 : 2, wifi_x - 20, compact ? 16 : 21);
    }
    ui_pixel_wifi_bars_create(top, wifi_x, 20, s_wifi_bars);
    s_battery = ui_pixel_battery_create(
        top, width - 28, 7, UI_TEXT, &lv_font_montserrat_14);
    update_status();
}

lv_obj_t *add_band(const char *left, const char *right) {
    const int width = screen_width();
    lv_obj_t *band = block(s_screen, 0, 26, width, 24, UI_ACCENT);
    if (left && left[0])
        label(band, left, &liuyao_title_20, UI_ON_ACCENT, 12, 0,
              width > 240 ? 170 : 102, 24);
    if (right && right[0]) {
        const int right_x = width > 240 ? 180 : 118;
        s_band_right = label(band, right, &liuyao_font_12, UI_ON_ACCENT,
                             right_x, 5, width - right_x - 8, 15,
                             LV_TEXT_ALIGN_RIGHT);
    }
    return band;
}

void add_footer(const char *primary, const char *secondary, int top = -1) {
    const int width = screen_width();
    if (top < 0) top = screen_height() == 240 ? 204 : screen_height() - 48;
    lv_obj_t *line = block(s_screen, 8, top, width - 16, 1, UI_BORDER);
    (void)line;
    s_footer_primary = label(s_screen, primary ? primary : "", &liuyao_font_12,
                             UI_TEXT, 8, top + 4, width - 16, 15);
    if (secondary && secondary[0]) {
        s_footer_secondary = label(s_screen, secondary, &liuyao_font_12,
                                   UI_TEXT_DIM, 8, top + 18, width - 16, 15,
                                   LV_TEXT_ALIGN_RIGHT);
    }
}

void draw_hexline(lv_obj_t *parent, int x, int y, bool yang, uint32_t color) {
    if (yang) {
        block(parent, x, y, 28, 3, color);
    } else {
        block(parent, x, y, 12, 3, color);
        block(parent, x + 16, y, 12, 3, color);
    }
}

uint8_t visible_fields(Field *fields) {
    uint8_t count = 0;
    if (s_input.method == LIUYAO_METHOD_NUMBER) fields[count++] = Field::Digits;
    fields[count++] = Field::Cast;
    return count;
}

const char *field_name(Field field) {
    switch (field) {
        case Field::Digits: return "数字";
        case Field::Year: return "年";
        case Field::Month: return "月";
        case Field::Day: return "日";
        case Field::Hour: return "时";
        case Field::Minute: return "分";
        case Field::TimeMode: return "时间制";
        case Field::City: return "校正地点";
        case Field::Cast: return "开始排盘";
        default: return "";
    }
}

void field_value(Field field, char *out, size_t capacity) {
    switch (field) {
        case Field::Digits:
            std::snprintf(out, capacity, "%s",
                          s_input.digit_count ? s_input.digits : "输入数字");
            break;
        case Field::Year:
            std::snprintf(out, capacity, "%d", s_input.year);
            break;
        case Field::Month:
            std::snprintf(out, capacity, "%u", s_input.month);
            break;
        case Field::Day:
            std::snprintf(out, capacity, "%u", s_input.day);
            break;
        case Field::Hour:
            std::snprintf(out, capacity, "%02u", s_input.hour);
            break;
        case Field::Minute:
            std::snprintf(out, capacity, "%02u", s_input.minute);
            break;
        case Field::TimeMode:
            std::snprintf(out, capacity, "%s",
                          s_input.time_mode == BAZI_TIME_TRUE_SOLAR ?
                          "真太阳时" : "标准时");
            break;
        case Field::City:
            std::snprintf(out, capacity, "%s", s_city_name);
            break;
        case Field::Cast:
            std::snprintf(out, capacity, "生成");
            break;
        default:
            out[0] = '\0';
            break;
    }
}

void normalize_day(void) {
    const uint8_t max_day = liuyao_input_max_day(s_input.year, s_input.month);
    if (max_day && s_input.day > max_day) s_input.day = max_day;
}

void adjust_field(Field field, int delta) {
    switch (field) {
        case Field::Year: {
            int year = s_input.year + (delta > 0 ? 1 : -1);
            if (year < 1900) year = 2100;
            if (year > 2100) year = 1900;
            s_input.year = (int16_t)year;
            normalize_day();
            break;
        }
        case Field::Month: {
            int month = (int)s_input.month + (delta > 0 ? 1 : -1);
            if (month < 1) month = 12;
            if (month > 12) month = 1;
            s_input.month = (uint8_t)month;
            normalize_day();
            break;
        }
        case Field::Day: {
            const uint8_t max_day = liuyao_input_max_day(s_input.year, s_input.month);
            if (!max_day) break;
            int day = (int)s_input.day + (delta > 0 ? 1 : -1);
            if (day < 1) day = max_day;
            if (day > max_day) day = 1;
            s_input.day = (uint8_t)day;
            break;
        }
        case Field::Hour:
            s_input.hour = (uint8_t)(((int)s_input.hour + (delta > 0 ? 1 : 23)) % 24);
            break;
        case Field::Minute:
            s_input.minute = liuyao_minute_step(s_input.minute, delta > 0 ? 1 : -1);
            break;
        case Field::TimeMode:
            s_input.time_mode = s_input.time_mode == BAZI_TIME_TRUE_SOLAR ?
                BAZI_TIME_STANDARD : BAZI_TIME_TRUE_SOLAR;
            break;
        case Field::City: {
            const uint8_t count = bazi_city_count();
            int city = s_city_from_home ? 0 : (int)s_input.city_id;
            if (!s_city_from_home) city += delta > 0 ? 1 : -1;
            if (city < 0) city = count - 1;
            if (city >= count) city = 0;
            s_input.city_id = (uint8_t)city;
            s_input.longitude_e4 = bazi_city_longitude_e4(s_input.city_id);
            std::snprintf(s_city_name, sizeof(s_city_name), "%s",
                          bazi_city_name(s_input.city_id));
            s_city_from_home = false;
            break;
        }
        default:
            break;
    }
}

void clamp_field(void) {
    Field fields[kFieldCount];
    const uint8_t count = visible_fields(fields);
    if (!count) {
        s_field_index = 0;
        return;
    }
    if (s_field_index >= count) s_field_index = count - 1;
}

void move_field(int delta) {
    Field fields[kFieldCount];
    const uint8_t count = visible_fields(fields);
    if (!count) return;
    int next = (int)s_field_index + delta;
    if (next < 0) next = count - 1;
    if (next >= count) next = 0;
    s_field_index = (uint8_t)next;
}

Field selected_field(void) {
    Field fields[kFieldCount];
    const uint8_t count = visible_fields(fields);
    if (!count || s_field_index >= count) return Field::Count;
    return fields[s_field_index];
}

void refresh_editor(void) {
    Field fields[kFieldCount];
    const uint8_t count = visible_fields(fields);
    clamp_field();
    if (count > kEditorRows) {
        if (s_field_index < s_first_visible) s_first_visible = s_field_index;
        if (s_field_index >= s_first_visible + kEditorRows)
            s_first_visible = (uint8_t)(s_field_index - kEditorRows + 1);
        if (s_first_visible > count - kEditorRows)
            s_first_visible = (uint8_t)(count - kEditorRows);
    } else {
        s_first_visible = 0;
    }

    for (uint8_t row = 0; row < kEditorRows; ++row) {
        const uint8_t index = (uint8_t)(s_first_visible + row);
        const bool visible = index < count;
        lv_obj_set_flag(s_rows[row], LV_OBJ_FLAG_HIDDEN, !visible);
        if (!visible) continue;
        const Field field = fields[index];
        const bool selected = index == s_field_index;
        const bool editing = selected && s_editor_editing;
        char value[64];
        field_value(field, value, sizeof(value));
        char display[72];
        if (editing && field != Field::Digits && field != Field::Cast)
            std::snprintf(display, sizeof(display), "< %s >", value);
        else
            std::snprintf(display, sizeof(display), "%s", value);
        set_text(s_row_names[row], field_name(field));
        set_text(s_row_values[row], display);
        const uint32_t bg = selected ? (editing ? UI_WARNING : UI_ACCENT) : UI_BG;
        ui_pixel_bg_color(s_rows[row], bg);
        ui_pixel_text_color(s_row_names[row], selected ? UI_ON_ACCENT : UI_TEXT);
        ui_pixel_text_color(s_row_values[row], selected ? UI_ON_ACCENT : UI_TEXT_DIM);
    }

    if (s_editor_message[0]) {
        set_text(s_footer_primary, s_editor_message);
    } else if (s_digit_editing) {
        set_text(s_footer_primary, "上下改数　确定下一位");
        set_text(s_footer_secondary, "长按上删　长按下预览");
    } else {
        set_text(s_footer_primary, "上下选择　确定修改");
        set_text(s_footer_secondary, "长按下键预览");
    }
}

void build_editor(void) {
    close_screen();
    s_mode = Mode::Editor;
    s_editor_editing = false;
    s_digit_editing = false;
    set_landscape(false);
    s_screen = lv_obj_create(nullptr);
    ui_pixel_background(s_screen);
    add_top_bar("六爻排盘");
    add_band(s_input.method == LIUYAO_METHOD_NUMBER ? "数字起卦" : "时间起卦",
             "");
    const bool tall = screen_height() > 240;
    const int width = screen_width() - 16;
    for (uint8_t row = 0; row < kEditorRows; ++row) {
        lv_obj_t *item = lv_obj_create(s_screen);
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(item, 8, (tall ? 58 : 54) + row * (tall ? 33 : 22));
        lv_obj_set_size(item, width, tall ? 31 : 21);
        lv_obj_set_style_radius(item, 0, 0);
        lv_obj_set_style_pad_all(item, 0, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        s_rows[row] = item;
        s_row_names[row] = label(item, "", &liuyao_font_12, UI_TEXT,
                                 8, tall ? 8 : 3, 104, 15);
        s_row_values[row] = label(item, "", &liuyao_font_12, UI_TEXT_DIM,
                                  112, tall ? 8 : 3, width - 120, 15,
                                  LV_TEXT_ALIGN_RIGHT);
    }
    int year = 0, month = 0, day = 0, hour = 0, minute = 0;
    char now[40] = "时间未同步";
    if (local_time(&year, &month, &day, &hour, &minute)) {
        std::snprintf(now, sizeof(now), "%04d-%02d-%02d %02d:%02d",
                      year, month, day, hour, minute);
    }
    label(s_screen, "起卦时间使用当前设备时间", &liuyao_font_12, UI_TEXT_DIM,
          8, tall ? 176 : 142, width, 15);
    label(s_screen, now, &liuyao_font_12, UI_TEXT,
          8, tall ? 194 : 160, width, 15);
    add_footer("上下选择　确定修改", "长按下键预览");
    refresh_editor();
    lv_screen_load(s_screen);
}

void refresh_method(void);

void build_method(void) {
    close_screen();
    s_mode = Mode::Method;
    set_landscape(false);
    s_screen = lv_obj_create(nullptr);
    ui_pixel_background(s_screen);
    add_top_bar("六爻排盘-起卦方式");
    const bool tall = screen_height() > 240;
    const int width = screen_width() - 16;
    for (uint8_t i = 0; i < 2; ++i) {
        const int y = tall ? 96 + i * 58 : 72 + i * 50;
        const int row_h = tall ? 48 : 40;
        lv_obj_t *row = panel(s_screen, 8, y, width, row_h);
        s_method_rows[i] = row;
        label(row, i == 0 ? "时间起卦" : "数字起卦", &liuyao_title_20,
              UI_TEXT, 12, tall ? 6 : 1, width - 24, 24);
        label(row, i == 0 ? "当前时间直接起卦" : "输入数字后预览",
              &liuyao_font_12, UI_TEXT_DIM, 12, tall ? 30 : 24,
              width - 24, 15);
    }
    add_footer("上下选择　确定进入", "长按确定返回");
    refresh_method();
    lv_screen_load(s_screen);
}

void refresh_method(void) {
    for (uint8_t i = 0; i < 2; ++i) {
        const bool selected = i == s_method_selected;
        ui_pixel_bg_color(s_method_rows[i],
                          selected ? UI_SURFACE_ALT : UI_SURFACE);
        lv_obj_set_style_border_color(s_method_rows[i],
            lv_color_hex(selected ? UI_ACCENT : UI_BORDER), 0);
        lv_obj_set_style_border_width(s_method_rows[i], selected ? 2 : 1, 0);
    }
    set_text(s_footer_primary, "上下选择　确定进入");
    set_text(s_footer_secondary, "长按确定返回");
}

void open_editor(uint8_t method) {
    s_input.method = method;
    s_method_selected = 1;
    s_field_index = 0;
    s_editor_message[0] = '\0';
    build_editor();
}

void build_preview(void);
void build_chart(void);
void build_meaning(void);

bool preview_input(void) {
    if (s_input.method == LIUYAO_METHOD_NUMBER) {
        int year = 0, month = 0, day = 0, hour = 0, minute = 0;
        if (!local_time(&year, &month, &day, &hour, &minute)) {
            s_editor_message[0] = '\0';
            std::snprintf(s_editor_message, sizeof(s_editor_message),
                          "时间未同步，请稍后重试");
            refresh_editor();
            return false;
        }
        s_input.year = (int16_t)year;
        s_input.month = (uint8_t)month;
        s_input.day = (uint8_t)day;
        s_input.hour = (uint8_t)hour;
        s_input.minute = (uint8_t)minute;
    }
    liuyao_chart_t chart;
    if (!liuyao_chart_create(&s_input, &chart)) {
        std::snprintf(s_editor_message, sizeof(s_editor_message),
                      "起卦资料无效，请检查");
        refresh_editor();
        return false;
    }
    s_chart = chart;
    s_editor_message[0] = '\0';
    build_preview();
    return true;
}

void cast_current_time(void) {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0;
    if (!local_time(&year, &month, &day, &hour, &minute)) {
        set_text(s_footer_primary, "时间未同步，请稍后重试");
        set_text(s_footer_secondary, "");
        return;
    }
    s_input.method = LIUYAO_METHOD_TIME;
    s_input.year = (int16_t)year;
    s_input.month = (uint8_t)month;
    s_input.day = (uint8_t)day;
    s_input.hour = (uint8_t)hour;
    s_input.minute = (uint8_t)minute;
    s_input.time_mode = BAZI_TIME_STANDARD;
    s_input.zi_rule = settings_get_late_zi() ? BAZI_ZI_LATE : BAZI_ZI_EARLY;
    liuyao_chart_t chart;
    if (!liuyao_chart_create(&s_input, &chart)) {
        set_text(s_footer_primary, "历法数据未覆盖");
        set_text(s_footer_secondary, "");
        return;
    }
    s_chart = chart;
    build_chart();
}

void preview_row(uint8_t index, const char *name, const char *value) {
    if (index >= 9) return;
    const int width = screen_width() - 16;
    const int row_h = 22;
    lv_obj_t *line = block(s_screen, 8, 54 + index * row_h, width, row_h, UI_BG);
    s_preview_lines[index] = line;
    label(line, name, &liuyao_font_12, UI_WARNING, 8, 4, 56, 15);
    label(line, value, &liuyao_font_12, UI_TEXT, 68, 4,
          width - 76, 15);
    if (index) block(s_screen, 8, 54 + index * row_h, width, 1, UI_BORDER);
}

void build_preview(void) {
    close_screen();
    s_mode = Mode::Preview;
    set_landscape(false);
    s_screen = lv_obj_create(nullptr);
    ui_pixel_background(s_screen);
    add_top_bar("六爻排盘");
    add_band("取数预览", "");

    char date[64];
    std::snprintf(date, sizeof(date), "%04d-%02u-%02u %02u:%02u",
                  s_chart.solar_year, s_chart.solar_month, s_chart.solar_day,
                  s_chart.solar_hour, s_chart.solar_minute);
    preview_row(0, "日期", date);
    char lunar[64];
    std::snprintf(lunar, sizeof(lunar), "农历 %u月%u日", s_chart.lunar_month,
                  s_chart.lunar_day);
    preview_row(1, "农历", lunar);
    char pillars[64];
    std::snprintf(pillars, sizeof(pillars), "%s%s %s%s %s%s %s%s",
                  bazi_stem_text(s_chart.year_stem),
                  bazi_branch_text(s_chart.year_branch),
                  bazi_stem_text(s_chart.month_stem),
                  bazi_branch_text(s_chart.month_branch),
                  bazi_stem_text(s_chart.day_stem), bazi_branch_text(s_chart.day_branch),
                  bazi_stem_text(s_chart.hour_stem),
                  bazi_branch_text(s_chart.hour_branch));
    preview_row(2, "干支", pillars);
    preview_row(3, "数字", s_input.digits);
    char numbers[64];
    std::snprintf(numbers, sizeof(numbers), "%u→%u %s",
                  s_chart.first_sum, s_chart.upper_number,
                  liuyao_trigram_name(s_chart.upper_number));
    preview_row(4, "上卦", numbers);
    std::snprintf(numbers, sizeof(numbers), "%u→%u %s",
                  s_chart.second_sum, s_chart.lower_number,
                  liuyao_trigram_name(s_chart.lower_number));
    preview_row(5, "下卦", numbers);
    std::snprintf(numbers, sizeof(numbers), "%u+%u+%u=%u /6余%u",
                  s_chart.upper_number, s_chart.lower_number, s_chart.hour_number,
                  s_chart.upper_number + s_chart.lower_number + s_chart.hour_number,
                  s_chart.moving_line);
    preview_row(6, "动数", numbers);
    const liuyao_line_t &moving = s_chart.lines[s_chart.moving_line - 1];
    char move[48];
    std::snprintf(move, sizeof(move), "第%u爻 %s",
                  s_chart.moving_line, marker_text(moving));
    preview_row(7, "变向", move);
    char hex[64];
    std::snprintf(hex, sizeof(hex), "%s 之 %s",
                  liuyao_hex_name(s_chart.original_id),
                  liuyao_hex_name(s_chart.changed_id));
    preview_row(8, "卦象", hex);
    add_footer("确定生成盘面", "长按下键返回编辑");
    lv_screen_load(s_screen);
}

void add_hex_title(uint8_t left, const char *kind, const char *name) {
    const int x = left ? 8 : 160;
    lv_obj_t *label_kind = label(s_screen, kind, &liuyao_font_12, UI_TEXT_DIM,
                                 x + 12, 90, 28, 24, LV_TEXT_ALIGN_CENTER);
    lv_obj_t *text = label(s_screen, name, &liuyao_title_20, UI_TEXT,
                           x + 42, 90, 100, 24, LV_TEXT_ALIGN_CENTER);
    lv_obj_t *bold = label(s_screen, name, &liuyao_title_20, UI_TEXT,
                           x + 43, 90, 100, 24, LV_TEXT_ALIGN_CENTER);
    (void)label_kind;
    (void)text;
    (void)bold;
}

void add_chart_rows(void) {
    char text[64];
    for (uint8_t i = 0; i < LIUYAO_LINE_COUNT; ++i) {
        const uint8_t visual = LIUYAO_LINE_COUNT - 1 - i;
        const liuyao_line_t &line = s_chart.lines[visual];
        lv_obj_t *left = lv_obj_create(s_screen);
        lv_obj_remove_flag(left, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(left, 8, 115 + i * 16);
        lv_obj_set_size(left, 152, 16);
        lv_obj_set_style_radius(left, 0, 0);
        lv_obj_set_style_pad_all(left, 0, 0);
        lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(left, 0, 0);

        label(left, kSpiritShort[line.spirit], &liuyao_font_12, UI_TEXT_DIM,
              0, 0, 20, 15, LV_TEXT_ALIGN_CENTER);
        std::snprintf(text, sizeof(text), "%s%s%s",
                      liuyao_relative_text(line.relative),
                      bazi_stem_text(line.stem), bazi_branch_text(line.branch));
        label(left, text, &liuyao_font_12, element_color(line.element),
              20, 0, 50, 15);
        draw_hexline(left, 74, 6, line.yang, UI_TEXT);
        const char *marker = marker_text(line);
        label(left, marker, &liuyao_font_12, UI_ACCENT, 103, 0, 28, 15,
              LV_TEXT_ALIGN_CENTER);
        const char *position = visual + 1 == s_chart.shi ? "世" :
            (visual + 1 == s_chart.ying ? "应" : "");
        label(left, position, &liuyao_font_12, UI_TEXT, 131, 0, 21, 15,
              LV_TEXT_ALIGN_CENTER);

        lv_obj_t *right = lv_obj_create(s_screen);
        lv_obj_remove_flag(right, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(right, 161, 115 + i * 16);
        lv_obj_set_size(right, 151, 16);
        lv_obj_set_style_radius(right, 0, 0);
        lv_obj_set_style_pad_all(right, 0, 0);
        lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(right, 0, 0);
        std::snprintf(text, sizeof(text), "%s%s%s",
                      liuyao_relative_text(line.changed_relative),
                      bazi_stem_text(line.changed_stem),
                      bazi_branch_text(line.changed_branch));
        label(right, text, &liuyao_font_12, element_color(line.changed_element),
              6, 0, 50, 15);
        const bool changed_yang = line.moving ? !line.yang : line.yang;
        draw_hexline(right, 64, 6, changed_yang, UI_TEXT);
    }
}

void meta_row_text(lv_obj_t *box, int y, const char *name, const char *value) {
    label(box, name, &buddy_font_16, UI_WARNING, 4, y, 36, 20);
    label(box, value, &buddy_font_16, UI_TEXT, 44, y, 252, 20);
}

void build_chart(void) {
    close_screen();
    s_mode = Mode::Chart;
    set_landscape(true);
    s_screen = lv_obj_create(nullptr);
    ui_pixel_background(s_screen);
    char title[32];
    std::snprintf(title, sizeof(title), "六爻排盘-%s",
                  s_input.method == LIUYAO_METHOD_NUMBER ?
                  "数字起卦" : "时间起卦");
    add_top_bar(title);

    lv_obj_t *meta = panel(s_screen, 8, 28, 304, 60);
    char text[96];
    std::snprintf(text, sizeof(text), "%04d-%02u-%02u %02u:%02u  农历%u月%u日",
                  s_chart.solar_year, s_chart.solar_month, s_chart.solar_day,
                  s_chart.solar_hour, s_chart.solar_minute,
                  s_chart.lunar_month, s_chart.lunar_day);
    meta_row_text(meta, 0, "日期", text);
    block(meta, 40, 0, 1, 58, UI_BORDER);
    block(meta, 0, 20, 304, 1, UI_BORDER);

    const char *pillars[4] = {};
    char p0[8], p1[8], p2[8], p3[8];
    std::snprintf(p0, sizeof(p0), "%s%s", bazi_stem_text(s_chart.year_stem),
                  bazi_branch_text(s_chart.year_branch));
    std::snprintf(p1, sizeof(p1), "%s%s", bazi_stem_text(s_chart.month_stem),
                  bazi_branch_text(s_chart.month_branch));
    std::snprintf(p2, sizeof(p2), "%s%s", bazi_stem_text(s_chart.day_stem),
                  bazi_branch_text(s_chart.day_branch));
    std::snprintf(p3, sizeof(p3), "%s%s", bazi_stem_text(s_chart.hour_stem),
                  bazi_branch_text(s_chart.hour_branch));
    pillars[0] = p0; pillars[1] = p1; pillars[2] = p2; pillars[3] = p3;
    for (uint8_t i = 0; i < 4; ++i) {
        label(meta, pillars[i], &buddy_font_16,
              i == 1 || i == 2 ? UI_DANGER : UI_TEXT,
              44 + i * 65, 20, 65, 20, LV_TEXT_ALIGN_CENTER);
    }
    block(meta, 0, 40, 304, 1, UI_BORDER);
    std::snprintf(text, sizeof(text), "%s%s  %s%s",
                  bazi_branch_text(s_chart.empty_branch[0]),
                  bazi_branch_text(s_chart.empty_branch[1]),
                  liuyao_palace_name(s_chart.palace),
                  liuyao_palace_state_text(s_chart.palace_index));
    meta_row_text(meta, 40, "旬空", text);

    lv_obj_t *sheet = panel(s_screen, 8, 89, 304, 127);
    (void)sheet;
    block(s_screen, 160, 89, 1, 127, UI_BORDER);
    add_hex_title(1, "本卦", liuyao_hex_name(s_chart.original_id));
    add_hex_title(0, "变卦", liuyao_hex_name(s_chart.changed_id));
    add_chart_rows();
    add_footer("确定看卦意  长按返回", "", 218);
    lv_screen_load(s_screen);
}

void meaning_column(uint8_t right, const char *kind, const char *name,
                    const char *meaning) {
    const int x = right ? 164 : 8;
    lv_obj_t *box = panel(s_screen, x, 58, 148, 148);
    label(box, kind, &liuyao_font_12, UI_WARNING, 8, 5, 132, 15,
          LV_TEXT_ALIGN_CENTER);
    lv_obj_t *title = label(box, name, &liuyao_title_20, UI_TEXT,
                            8, 22, 132, 24, LV_TEXT_ALIGN_CENTER);
    lv_obj_t *bold = label(box, name, &liuyao_title_20, UI_TEXT,
                           9, 22, 132, 24, LV_TEXT_ALIGN_CENTER);
    lv_obj_t *body = ui_pixel_label(box, meaning, &liuyao_font_12, UI_TEXT);
    lv_obj_set_pos(body, 4, 50);
    lv_obj_set_size(body, 140, 90);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_line_space(body, 2, 0);
    (void)title;
    (void)bold;
}

void build_meaning(void) {
    close_screen();
    s_mode = Mode::Meaning;
    set_landscape(true);
    s_screen = lv_obj_create(nullptr);
    ui_pixel_background(s_screen);
    add_top_bar("");
    add_band("卦意", "");
    meaning_column(0, "本卦", liuyao_hex_name(s_chart.original_id),
                   liuyao_hex_explanation(s_chart.original_id));
    meaning_column(1, "变卦", liuyao_hex_name(s_chart.changed_id),
                   liuyao_hex_explanation(s_chart.changed_id));
    add_footer("确定回盘  长按回盘", "", 214);
    lv_screen_load(s_screen);
}

void preview_key(uint8_t btn, app_btn_event_t event) {
    if (event == BTN_EVT_CLICK && btn == BSP_BTN_OK) {
        build_chart();
    } else if (event == BTN_EVT_LONG && btn == BSP_BTN_DOWN) {
        build_editor();
    }
}

void chart_key(uint8_t btn, app_btn_event_t event) {
    if (event != BTN_EVT_CLICK) return;
    if (btn == BSP_BTN_OK) build_meaning();
}

void meaning_key(uint8_t btn, app_btn_event_t event) {
    if (event == BTN_EVT_CLICK && btn == BSP_BTN_OK) build_chart();
}

void editor_key(uint8_t btn, app_btn_event_t event) {
    const Field field = selected_field();
    if (event == BTN_EVT_LONG && btn == BSP_BTN_DOWN) {
        preview_input();
        return;
    }
    if (event == BTN_EVT_LONG && btn == BSP_BTN_UP &&
        s_digit_editing && field == Field::Digits) {
        if (s_input.digit_count) {
            if (s_digit_cursor < s_input.digit_count - 1) {
                std::memmove(&s_input.digits[s_digit_cursor],
                             &s_input.digits[s_digit_cursor + 1],
                             s_input.digit_count - s_digit_cursor - 1);
            }
            s_input.digit_count--;
            s_input.digits[s_input.digit_count] = '\0';
            if (!s_input.digit_count) s_digit_editing = false;
            if (s_digit_cursor >= s_input.digit_count && s_digit_cursor)
                s_digit_cursor = (uint8_t)(s_input.digit_count - 1);
        }
        refresh_editor();
        return;
    }
    if (event != BTN_EVT_CLICK) return;

    if (s_digit_editing && field == Field::Digits) {
        if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
            int digit = s_input.digits[s_digit_cursor] - '0';
            digit = (digit + (btn == BSP_BTN_UP ? 1 : 9)) % 10;
            s_input.digits[s_digit_cursor] = (char)('0' + digit);
        } else if (btn == BSP_BTN_OK) {
            if (s_digit_cursor + 1 < s_input.digit_count) {
                s_digit_cursor++;
            } else if (s_input.digit_count < LIUYAO_MAX_DIGITS) {
                s_input.digits[s_input.digit_count++] = '0';
                s_input.digits[s_input.digit_count] = '\0';
                s_digit_cursor = (uint8_t)(s_input.digit_count - 1);
            } else {
                s_digit_editing = false;
                move_field(1);
            }
        }
        refresh_editor();
        return;
    }

    if (s_editor_editing) {
        if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
            adjust_field(field, btn == BSP_BTN_UP ? 1 : -1);
        } else if (btn == BSP_BTN_OK) {
            s_editor_editing = false;
            move_field(1);
        }
        refresh_editor();
        return;
    }

    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        move_field(btn == BSP_BTN_UP ? -1 : 1);
    } else if (btn == BSP_BTN_OK) {
        if (field == Field::Cast) {
            preview_input();
            return;
        }
        if (field == Field::Digits) {
            s_digit_editing = true;
            if (!s_input.digit_count) {
                s_input.digits[0] = '0';
                s_input.digits[1] = '\0';
                s_input.digit_count = 1;
                s_digit_cursor = 0;
            }
        } else {
            s_editor_editing = true;
        }
    }
    refresh_editor();
}

void start(void) {
    if (!bsp_lvgl_lock(1000)) return;
    if (s_screen) {
        bsp_lvgl_unlock();
        return;
    }
    if (!s_form_initialized) {
        initialize_input();
        s_form_initialized = true;
    }
    set_landscape(false);
    s_method_selected = 0;
    build_method();
    ESP_LOGI(kTag, "started portrait; free=%u largest=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    bsp_lvgl_unlock();
}

void stop(void) {
    if (!bsp_lvgl_lock(-1)) return;
    close_screen();
    set_landscape(false);
    s_mode = Mode::Method;
    s_editor_editing = false;
    s_digit_editing = false;
    ESP_LOGI(kTag, "stopped; free=%u largest=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    bsp_lvgl_unlock();
}

void on_key(uint8_t btn, app_btn_event_t event, uint16_t) {
    if (!bsp_lvgl_lock(500)) return;
    switch (s_mode) {
        case Mode::Method:
            if (event == BTN_EVT_CLICK) {
                if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
                    int next = (int)s_method_selected +
                        (btn == BSP_BTN_DOWN ? 1 : -1);
                    if (next < 0) next = 1;
                    if (next > 1) next = 0;
                    s_method_selected = (uint8_t)next;
                    refresh_method();
                } else if (btn == BSP_BTN_OK) {
                    if (s_method_selected == 0) {
                        cast_current_time();
                    } else {
                        open_editor(LIUYAO_METHOD_NUMBER);
                    }
                }
            }
            break;
        case Mode::Editor: editor_key(btn, event); break;
        case Mode::Preview: preview_key(btn, event); break;
        case Mode::Chart: chart_key(btn, event); break;
        case Mode::Meaning: meaning_key(btn, event); break;
    }
    bsp_lvgl_unlock();
}

bool on_back(void) {
    if (!bsp_lvgl_lock(500)) return true;
    bool handled = true;
    switch (s_mode) {
        case Mode::Method:
            handled = false;
            break;
        case Mode::Editor:
            build_method();
            break;
        case Mode::Preview:
            build_editor();
            break;
        case Mode::Chart:
            if (s_input.method == LIUYAO_METHOD_NUMBER) build_editor();
            else build_method();
            break;
        case Mode::Meaning:
            build_chart();
            break;
    }
    bsp_lvgl_unlock();
    return handled;
}

bool prepare_stop(void) {
    return true;
}

} // namespace
} // namespace liuyao

extern const passport_app_t g_liuyao_app = {
    .id = APP_ID_LIUYAO,
    .name = "六爻排盘",
    .en_name = "LIU YAO",
    .desc = "时间数字\n六爻排盘",
    .tag = "本地历法",
    .theme_color = 0x2A9D9B,
    .init = nullptr,
    .start = liuyao::start,
    .stop = liuyao::stop,
    .on_key = liuyao::on_key,
    .prepare_stop = liuyao::prepare_stop,
    .on_back = liuyao::on_back,
};
