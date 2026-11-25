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
#include "esp_random.h"
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
constexpr uint8_t kCoinCount = 3;
constexpr uint8_t kShakeTicks = 12;
constexpr uint32_t kShakeTickMs = 60;
constexpr int kCoinSize = 48;
constexpr int kCoinX[kCoinCount] = {24, 88, 152};
constexpr int kCoinY = 21;
constexpr char kTag[] = "liuyao";

int screen_width(void) {
    return lv_display_get_horizontal_resolution(lv_display_get_default());
}

int screen_height(void) {
    return lv_display_get_vertical_resolution(lv_display_get_default());
}

void set_landscape(bool landscape) {
    // 270 keeps the three physical keys on the top edge when the device is
    // held horizontally. 90 puts them on the bottom edge.
    const lv_display_rotation_t wanted =
        landscape ? LV_DISPLAY_ROTATION_270 : LV_DISPLAY_ROTATION_0;
    lv_display_t *display = lv_display_get_default();
    if (display && lv_display_get_rotation(display) != wanted)
        lv_display_set_rotation(display, wanted);
}

enum class Mode : uint8_t {
    Method,
    Shake,
    Chart,
    Meaning,
};

Mode s_mode = Mode::Method;
liuyao_input_t s_input{};
liuyao_chart_t s_chart{};
bool s_form_initialized = false;
uint8_t s_method_selected = 0;
bool s_chart_ready = false;
bool s_time_ok = false;
bool s_calendar_error = false;
uint8_t s_toss_count = 0;
uint8_t s_face_values[LIUYAO_LINE_COUNT][kCoinCount] = {};
uint8_t s_line_values[LIUYAO_LINE_COUNT] = {};
uint8_t s_pending_faces[kCoinCount] = {};
bool s_shaking = false;
uint8_t s_shake_ticks = 0;
lv_timer_t *s_shake_timer = nullptr;

lv_obj_t *s_screen = nullptr;
lv_obj_t *s_method_rows[2] = {};
lv_obj_t *s_coins[kCoinCount] = {};
lv_obj_t *s_coin_inner[kCoinCount] = {};
lv_obj_t *s_coin_faces[kCoinCount] = {};
lv_obj_t *s_coin_ticks[kCoinCount][2] = {};
lv_obj_t *s_row_boxes[LIUYAO_LINE_COUNT] = {};
lv_obj_t *s_row_names[LIUYAO_LINE_COUNT] = {};
lv_obj_t *s_row_tosses[LIUYAO_LINE_COUNT] = {};
lv_obj_t *s_row_values[LIUYAO_LINE_COUNT] = {};
lv_obj_t *s_row_marks[LIUYAO_LINE_COUNT] = {};
lv_obj_t *s_row_yang[LIUYAO_LINE_COUNT] = {};
lv_obj_t *s_row_yin_left[LIUYAO_LINE_COUNT] = {};
lv_obj_t *s_row_yin_right[LIUYAO_LINE_COUNT] = {};
lv_obj_t *s_row_current[LIUYAO_LINE_COUNT] = {};
lv_obj_t *s_band_meta = nullptr;
lv_obj_t *s_status_left = nullptr;
lv_obj_t *s_status_right = nullptr;
lv_obj_t *s_footer_primary = nullptr;
lv_obj_t *s_footer_secondary = nullptr;

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

bool chart_has_change(void) {
    return s_chart.changed_id != s_chart.original_id;
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
            return;
        }
    }
    s_input.city_id = 0;
    s_input.longitude_e4 = bazi_city_longitude_e4(0);
}

void initialize_input(void) {
    std::memset(&s_input, 0, sizeof(s_input));
    int year = 2000, month = 1, day = 1, hour = 12, minute = 0;
    if (!local_time(&year, &month, &day, &hour, &minute)) {
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
    initialize_location();
}

void close_screen(void) {
    if (s_shake_timer) {
        lv_timer_delete(s_shake_timer);
        s_shake_timer = nullptr;
    }
    s_shaking = false;
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = nullptr;
    }
    std::memset(s_method_rows, 0, sizeof(s_method_rows));
    std::memset(s_coins, 0, sizeof(s_coins));
    std::memset(s_coin_inner, 0, sizeof(s_coin_inner));
    std::memset(s_coin_faces, 0, sizeof(s_coin_faces));
    std::memset(s_coin_ticks, 0, sizeof(s_coin_ticks));
    std::memset(s_row_boxes, 0, sizeof(s_row_boxes));
    std::memset(s_row_names, 0, sizeof(s_row_names));
    std::memset(s_row_tosses, 0, sizeof(s_row_tosses));
    std::memset(s_row_values, 0, sizeof(s_row_values));
    std::memset(s_row_marks, 0, sizeof(s_row_marks));
    std::memset(s_row_yang, 0, sizeof(s_row_yang));
    std::memset(s_row_yin_left, 0, sizeof(s_row_yin_left));
    std::memset(s_row_yin_right, 0, sizeof(s_row_yin_right));
    std::memset(s_row_current, 0, sizeof(s_row_current));
    s_band_meta = s_status_left = s_status_right = nullptr;
    s_footer_primary = s_footer_secondary = nullptr;
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
    lv_obj_t *top = block(s_screen, 0, 0, width, 26, UI_SURFACE);
    lv_obj_set_style_border_width(top, 1, 0);
    lv_obj_set_style_border_side(top, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(top, lv_color_hex(UI_BORDER), 0);
    // Match the other apps: signal on the left, title after it, battery right.
    ui_pixel_wifi_bars_create(top, UI_STATUS_WIFI_X, 18, s_wifi_bars);
    if (title && title[0]) {
        const bool compact = width <= 240;
        lv_obj_t *name = label(top, title,
                               compact ? &liuyao_font_12 : &buddy_font_16,
                               UI_TEXT, 0, 0, width - 78, compact ? 16 : 24);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 42, 0);
    }
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
        s_band_meta = label(band, right, &liuyao_font_12, UI_ON_ACCENT,
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

const char *face_text(uint8_t value) {
    return value == 3 ? "背" : "字";
}

const char *value_name(uint8_t value) {
    switch (value) {
        case 6: return "老阴";
        case 7: return "少阳";
        case 8: return "少阴";
        default: return "老阳";
    }
}

const char *value_marker(uint8_t value) {
    if (value == 6) return "X →";
    if (value == 9) return "O →";
    return "";
}

const char *line_name(uint8_t index) {
    static const char *const names[LIUYAO_LINE_COUNT] = {
        "初爻", "二爻", "三爻", "四爻", "五爻", "上爻",
    };
    return index < LIUYAO_LINE_COUNT ? names[index] : "";
}

void reset_manual_state(void) {
    s_toss_count = 0;
    s_chart_ready = false;
    s_calendar_error = false;
    s_shaking = false;
    s_shake_ticks = 0;
    std::memset(s_face_values, 0, sizeof(s_face_values));
    std::memset(s_line_values, 0, sizeof(s_line_values));
    std::memset(s_pending_faces, 0, sizeof(s_pending_faces));
    std::memset(s_input.line_values, 0, sizeof(s_input.line_values));
    if (s_shake_timer) {
        lv_timer_delete(s_shake_timer);
        s_shake_timer = nullptr;
    }
}

bool capture_manual_time(void) {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0;
    if (!local_time(&year, &month, &day, &hour, &minute)) return false;
    s_input.method = LIUYAO_METHOD_MANUAL;
    s_input.year = (int16_t)year;
    s_input.month = (uint8_t)month;
    s_input.day = (uint8_t)day;
    s_input.hour = (uint8_t)hour;
    s_input.minute = (uint8_t)minute;
    s_input.time_mode = BAZI_TIME_STANDARD;
    s_input.zi_rule = settings_get_late_zi() ? BAZI_ZI_LATE : BAZI_ZI_EARLY;
    return true;
}

bool create_manual_chart(void) {
    liuyao_chart_t chart;
    std::memcpy(s_input.line_values, s_line_values, sizeof(s_line_values));
    if (!liuyao_chart_create(&s_input, &chart)) {
        s_chart_ready = false;
        return false;
    }
    s_chart = chart;
    s_chart_ready = true;
    return true;
}

void build_shake(void);
void build_chart(void);
void build_meaning(void);

void open_manual(void) {
    reset_manual_state();
    s_method_selected = 1;
    s_time_ok = capture_manual_time();
    build_shake();
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
        label(row, i == 0 ? "时间起卦" : "手动摇卦", &liuyao_title_20,
              UI_TEXT, 12, tall ? 6 : 1, width - 24, 24);
        label(row, i == 0 ? "当前时间直接起卦" : "三枚铜钱摇六次",
              &liuyao_font_12, UI_TEXT_DIM, 12, tall ? 30 : 24,
              width - 24, 15);
    }
    add_footer("上下选择　确定进入", "长按确定返回");
    refresh_method();
    lv_screen_load(s_screen);
}

void place_coin_ticks(uint8_t i, int x, int y) {
    lv_obj_set_pos(s_coin_ticks[i][0], x - 10, y + 21);
    lv_obj_set_pos(s_coin_ticks[i][1], x + kCoinSize + 2, y + 21);
}

void show_row_glyph(uint8_t visual, bool visible, bool yang) {
    lv_obj_set_flag(s_row_yang[visual], LV_OBJ_FLAG_HIDDEN, !(visible && yang));
    lv_obj_set_flag(s_row_yin_left[visual], LV_OBJ_FLAG_HIDDEN,
                    !(visible && !yang));
    lv_obj_set_flag(s_row_yin_right[visual], LV_OBJ_FLAG_HIDDEN,
                    !(visible && !yang));
}

void refresh_shake(void) {
    const bool complete = s_toss_count >= LIUYAO_LINE_COUNT;
    if (s_band_meta) {
        char meta[24];
        if (complete) {
            std::snprintf(meta, sizeof(meta), "六爻已成");
        } else {
            std::snprintf(meta, sizeof(meta), "第 %u / 6 爻",
                          (unsigned)(s_toss_count + 1));
        }
        set_text(s_band_meta, meta);
    }

    for (uint8_t i = 0; i < kCoinCount; ++i) {
        lv_obj_set_style_border_color(
            s_coins[i], lv_color_hex(s_shaking ? UI_ACCENT : UI_BORDER), 0);
        if (s_shaking) {
            set_text(s_coin_faces[i],
                     ((s_shake_ticks + i) & 1U) ? "背" : "字");
            lv_obj_set_flag(s_coin_inner[i], LV_OBJ_FLAG_HIDDEN, true);
        } else if (s_toss_count > 0) {
            const uint8_t *faces = s_face_values[s_toss_count - 1];
            set_text(s_coin_faces[i], face_text(faces[i]));
            lv_obj_set_flag(s_coin_inner[i], LV_OBJ_FLAG_HIDDEN, true);
        } else {
            set_text(s_coin_faces[i], "");
            lv_obj_set_flag(s_coin_inner[i], LV_OBJ_FLAG_HIDDEN, false);
        }
        for (uint8_t t = 0; t < 2; ++t) {
            lv_obj_set_flag(s_coin_ticks[i][t], LV_OBJ_FLAG_HIDDEN,
                            !s_shaking);
            if (s_shaking) place_coin_ticks(i, kCoinX[i], kCoinY);
        }
        if (!s_shaking)
            lv_obj_set_pos(s_coins[i], kCoinX[i], kCoinY);
    }

    for (uint8_t visual = 0; visual < LIUYAO_LINE_COUNT; ++visual) {
        const uint8_t line = (uint8_t)(LIUYAO_LINE_COUNT - 1 - visual);
        const bool done = line < s_toss_count;
        const bool current = !complete && !s_shaking && line == s_toss_count;
        const bool active = !complete && s_shaking && line == s_toss_count;
        const bool selected = current || active;
        ui_pixel_bg_color(s_row_boxes[visual],
                          selected ? UI_SURFACE_ALT : UI_BG);
        lv_obj_set_flag(s_row_current[visual], LV_OBJ_FLAG_HIDDEN, !selected);
        set_text(s_row_names[visual], line_name(line));
        ui_pixel_text_color(s_row_names[visual],
                            done ? UI_TEXT : UI_TEXT_DIM);

        char toss[16] = {};
        char value[20] = {};
        if (done) {
            const uint8_t *faces = s_face_values[line];
            const uint8_t v = s_line_values[line];
            std::snprintf(toss, sizeof(toss), "%s%s%s",
                          face_text(faces[0]), face_text(faces[1]),
                          face_text(faces[2]));
            std::snprintf(value, sizeof(value), "%s %u",
                          value_name(v), (unsigned)v);
            ui_pixel_text_color(s_row_values[visual],
                                v == 6 || v == 9 ? UI_WARNING : UI_TEXT);
            set_text(s_row_marks[visual], value_marker(v));
            show_row_glyph(visual, true, v == 7 || v == 9);
        } else if (active) {
            std::snprintf(value, sizeof(value), "摇动中");
            ui_pixel_text_color(s_row_values[visual], UI_ACCENT);
            set_text(s_row_marks[visual], "");
            show_row_glyph(visual, false, false);
        } else {
            std::snprintf(value, sizeof(value), "%s",
                          current ? "待摇" : "未摇");
            ui_pixel_text_color(s_row_values[visual], UI_TEXT_DIM);
            set_text(s_row_marks[visual], "");
            show_row_glyph(visual, false, false);
        }
        set_text(s_row_tosses[visual], toss);
        set_text(s_row_values[visual], value);
    }

    if (!s_time_ok) {
        set_text(s_status_left, "时间未同步，请稍后重试");
        set_text(s_status_right, "");
        set_text(s_footer_primary, "确定重试");
        set_text(s_footer_secondary, "返回键回起卦方式");
        return;
    }
    if (s_calendar_error) {
        set_text(s_status_left, "历法数据未覆盖");
        set_text(s_status_right, "");
        set_text(s_footer_primary, complete ? "确定重试" : "确定摇卦");
        set_text(s_footer_secondary, "长按确定重摇");
        return;
    }
    if (s_shaking) {
        set_text(s_status_left, "摇动中");
        set_text(s_status_right, "确定落定");
        set_text(s_footer_primary, "摇动中");
        set_text(s_footer_secondary, "确定落定");
        return;
    }
    if (complete) {
        char left[32];
        char right[32];
        std::snprintf(left, sizeof(left), "本卦 %s",
                      liuyao_hex_name(s_chart.original_id));
        if (s_chart.changed_id != s_chart.original_id) {
            std::snprintf(right, sizeof(right), "之 %s",
                          liuyao_hex_name(s_chart.changed_id));
        } else {
            std::snprintf(right, sizeof(right), "无动爻");
        }
        set_text(s_status_left, left);
        set_text(s_status_right, right);
        set_text(s_footer_primary, "确定看盘");
        set_text(s_footer_secondary, "长按确定重摇");
        return;
    }
    if (s_toss_count == 0) {
        set_text(s_status_left, "按确定摇第一爻");
        set_text(s_status_right, "由下而上");
        set_text(s_footer_primary, "确定摇卦");
        set_text(s_footer_secondary, "长按确定重摇");
        return;
    }
    const uint8_t last = s_toss_count - 1;
    const uint8_t *faces = s_face_values[last];
    char sum[32];
    std::snprintf(sum, sizeof(sum), "%s%s%s  %s %u",
                  face_text(faces[0]), face_text(faces[1]),
                  face_text(faces[2]), value_name(s_line_values[last]),
                  (unsigned)s_line_values[last]);
    const char *marker = value_marker(s_line_values[last]);
    set_text(s_status_left, sum);
    set_text(s_status_right, marker[0] ? marker : "静");
    set_text(s_footer_primary, "确定摇下一爻");
    set_text(s_footer_secondary, "长按确定重摇");
}

void finish_shake(void) {
    if (!s_shaking) return;
    s_shaking = false;
    if (s_shake_timer) {
        lv_timer_delete(s_shake_timer);
        s_shake_timer = nullptr;
    }
    const uint8_t index = s_toss_count;
    for (uint8_t i = 0; i < kCoinCount; ++i) {
        s_face_values[index][i] = s_pending_faces[i];
        lv_obj_set_pos(s_coins[i], kCoinX[i], kCoinY);
    }
    ++s_toss_count;
    if (s_toss_count >= LIUYAO_LINE_COUNT) {
        s_calendar_error = !create_manual_chart();
    }
    refresh_shake();
}

void shake_tick(lv_timer_t *) {
    if (!s_shaking) return;
    ++s_shake_ticks;
    if (s_shake_ticks >= kShakeTicks) {
        finish_shake();
        return;
    }
    for (uint8_t i = 0; i < kCoinCount; ++i) {
        const int dx = ((s_shake_ticks + i * 2) % 3 - 1) * 4;
        const int dy = ((s_shake_ticks + i) % 3 - 1) * 2;
        lv_obj_set_pos(s_coins[i], kCoinX[i] + dx, kCoinY + dy);
        place_coin_ticks(i, kCoinX[i] + dx, kCoinY + dy);
        set_text(s_coin_faces[i],
                 ((s_shake_ticks + i) & 1U) ? "背" : "字");
    }
}

void start_shake(void) {
    if (s_shaking || s_toss_count >= LIUYAO_LINE_COUNT) return;
    if (!s_time_ok) return;
    uint8_t sum = 0;
    for (uint8_t i = 0; i < kCoinCount; ++i) {
        const uint8_t face = (esp_random() & 1U) ? 3 : 2;
        s_pending_faces[i] = face;
        sum = (uint8_t)(sum + face);
    }
    s_line_values[s_toss_count] = sum;
    s_shaking = true;
    s_shake_ticks = 0;
    if (s_shake_timer) {
        lv_timer_delete(s_shake_timer);
        s_shake_timer = nullptr;
    }
    s_shake_timer = lv_timer_create(shake_tick, kShakeTickMs, nullptr);
    refresh_shake();
}

void build_shake(void) {
    close_screen();
    s_mode = Mode::Shake;
    set_landscape(false);
    s_screen = lv_obj_create(nullptr);
    ui_pixel_background(s_screen);
    add_top_bar("六爻排盘-手动摇卦");
    add_band("手动摇卦", "第 1 / 6 爻");
    lv_obj_t *tray = panel(s_screen, 8, 58, 224, 90);
    for (uint8_t i = 0; i < kCoinCount; ++i) {
        lv_obj_t *coin = lv_obj_create(tray);
        lv_obj_remove_flag(coin, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(coin, kCoinX[i], kCoinY);
        lv_obj_set_size(coin, kCoinSize, kCoinSize);
        lv_obj_set_style_radius(coin, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_all(coin, 0, 0);
        lv_obj_set_style_bg_color(coin, lv_color_hex(UI_SURFACE_ALT), 0);
        lv_obj_set_style_bg_opa(coin, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(coin, 2, 0);
        lv_obj_set_style_border_color(coin, lv_color_hex(UI_BORDER), 0);
        s_coins[i] = coin;

        lv_obj_t *inner = lv_obj_create(coin);
        lv_obj_remove_flag(inner, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(inner, 26, 26);
        lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_all(inner, 0, 0);
        lv_obj_set_style_bg_opa(inner, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(inner, 1, 0);
        lv_obj_set_style_border_color(inner, lv_color_hex(UI_BORDER), 0);
        lv_obj_align(inner, LV_ALIGN_CENTER, 0, 0);
        s_coin_inner[i] = inner;

        lv_obj_t *face = label(coin, "", &liuyao_title_20, UI_TEXT,
                               0, 0, 44, 24, LV_TEXT_ALIGN_CENTER);
        lv_obj_align(face, LV_ALIGN_CENTER, 0, 0);
        s_coin_faces[i] = face;

        for (uint8_t t = 0; t < 2; ++t) {
            lv_obj_t *tick = block(tray, 0, 0, 2, 6, UI_ACCENT);
            lv_obj_set_flag(tick, LV_OBJ_FLAG_HIDDEN, true);
            s_coin_ticks[i][t] = tick;
        }
    }

    s_status_left = label(s_screen, "", &liuyao_font_12, UI_TEXT_DIM,
                          8, 152, 140, 15);
    s_status_right = label(s_screen, "", &liuyao_font_12, UI_TEXT_DIM,
                           160, 152, 72, 15, LV_TEXT_ALIGN_RIGHT);

    for (uint8_t visual = 0; visual < LIUYAO_LINE_COUNT; ++visual) {
        lv_obj_t *row = lv_obj_create(s_screen);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(row, 8, 174 + visual * 16);
        lv_obj_set_size(row, 224, 16);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(UI_BG), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(UI_BORDER), 0);
        s_row_boxes[visual] = row;

        s_row_current[visual] = block(row, 0, 0, 2, 16, UI_ACCENT);
        s_row_names[visual] = label(row, "", &liuyao_font_12, UI_TEXT_DIM,
                                    6, 0, 30, 15);
        s_row_tosses[visual] = label(row, "", &liuyao_font_12, UI_TEXT_DIM,
                                     38, 0, 44, 15);
        s_row_yang[visual] = block(row, 86, 6, 28, 3, UI_TEXT);
        s_row_yin_left[visual] = block(row, 86, 6, 12, 3, UI_TEXT);
        s_row_yin_right[visual] = block(row, 102, 6, 12, 3, UI_TEXT);
        s_row_values[visual] = label(row, "", &liuyao_font_12, UI_TEXT_DIM,
                                     120, 0, 60, 15);
        s_row_marks[visual] = label(row, "", &liuyao_font_12, UI_ACCENT,
                                    182, 0, 38, 15, LV_TEXT_ALIGN_RIGHT);
    }

    add_footer("确定摇卦", "长按确定重摇", 272);
    refresh_shake();
    lv_screen_load(s_screen);
}

void shake_key(uint8_t btn, app_btn_event_t event) {
    if (event == BTN_EVT_LONG && btn == BSP_BTN_OK) {
        reset_manual_state();
        s_time_ok = capture_manual_time();
        build_shake();
        return;
    }
    if (event != BTN_EVT_CLICK || btn != BSP_BTN_OK) return;
    if (!s_time_ok) {
        s_time_ok = capture_manual_time();
        refresh_shake();
        return;
    }
    if (s_calendar_error) {
        if (s_toss_count >= LIUYAO_LINE_COUNT)
            s_calendar_error = !create_manual_chart();
        refresh_shake();
        return;
    }
    if (s_shaking) {
        finish_shake();
        return;
    }
    if (s_toss_count >= LIUYAO_LINE_COUNT) {
        if (s_chart_ready) build_chart();
        return;
    }
    start_shake();
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
    s_chart_ready = true;
    build_chart();
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
        lv_obj_set_pos(left, 8, 115 + i * 19);
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

        if (!chart_has_change()) continue;

        lv_obj_t *right = lv_obj_create(s_screen);
        lv_obj_remove_flag(right, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(right, 161, 115 + i * 19);
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
                  s_input.method == LIUYAO_METHOD_MANUAL ?
                  "手动摇卦" : "时间起卦");
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

    // The chart uses the full height; the old bottom hint row is gone.
    lv_obj_t *sheet = panel(s_screen, 8, 89, 304, 143);
    (void)sheet;
    block(s_screen, 160, 89, 1, 143, UI_BORDER);
    add_hex_title(1, "本卦", liuyao_hex_name(s_chart.original_id));
    if (chart_has_change()) {
        add_hex_title(0, "变卦", liuyao_hex_name(s_chart.changed_id));
    } else {
        label(s_screen, "无动爻", &liuyao_title_20, UI_TEXT_DIM,
              160, 90, 152, 24, LV_TEXT_ALIGN_CENTER);
    }
    add_chart_rows();
    lv_screen_load(s_screen);
}

void meaning_column(uint8_t right, const char *kind, const char *name,
                    const char *meaning) {
    const int x = right ? 164 : 8;
    lv_obj_t *box = panel(s_screen, x, 34, 148, 172);
    label(box, kind, &liuyao_font_12, UI_WARNING, 8, 5, 132, 15,
          LV_TEXT_ALIGN_CENTER);
    lv_obj_t *title = label(box, name, &liuyao_title_20, UI_TEXT,
                            8, 22, 132, 24, LV_TEXT_ALIGN_CENTER);
    lv_obj_t *bold = label(box, name, &liuyao_title_20, UI_TEXT,
                           9, 22, 132, 24, LV_TEXT_ALIGN_CENTER);
    lv_obj_t *body = ui_pixel_label(box, meaning, &liuyao_font_12, UI_TEXT);
    lv_obj_set_pos(body, 4, 50);
    lv_obj_set_size(body, 140, 114);
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
    add_top_bar("六爻排盘-卦意");
    meaning_column(0, "本卦", liuyao_hex_name(s_chart.original_id),
                   liuyao_hex_explanation(s_chart.original_id));
    if (chart_has_change()) {
        meaning_column(1, "变卦", liuyao_hex_name(s_chart.changed_id),
                       liuyao_hex_explanation(s_chart.changed_id));
    } else {
        label(s_screen, "无动爻", &liuyao_title_20, UI_TEXT_DIM,
              164, 100, 148, 24, LV_TEXT_ALIGN_CENTER);
    }
    add_footer("确定回盘  长按回盘", "", 214);
    lv_screen_load(s_screen);
}

void chart_key(uint8_t btn, app_btn_event_t event) {
    if (event != BTN_EVT_CLICK) return;
    if (btn == BSP_BTN_OK) build_meaning();
}

void meaning_key(uint8_t btn, app_btn_event_t event) {
    if (event == BTN_EVT_CLICK && btn == BSP_BTN_OK) build_chart();
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
                        open_manual();
                    }
                }
            }
            break;
        case Mode::Shake: shake_key(btn, event); break;
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
        case Mode::Shake:
            build_method();
            break;
        case Mode::Chart:
            if (s_input.method == LIUYAO_METHOD_MANUAL) build_shake();
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
    .desc = "时间摇卦\n六爻排盘",
    .tag = "本地历法",
    .theme_color = 0x2A9D9B,
    .init = nullptr,
    .start = liuyao::start,
    .stop = liuyao::stop,
    .on_key = liuyao::on_key,
    .prepare_stop = liuyao::prepare_stop,
    .on_back = liuyao::on_back,
};
