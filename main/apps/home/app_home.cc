// 首页(个人主页)应用。
//
// 小内存约束下，网络 IO 一律在不持 LVGL 锁时完成；stop() 必须删屏，
// 因为 C3 内部堆没有余量同时容纳两个应用的界面。
//
// 2026-09-13 视觉精致化(全部为"对象拼画 + 低频动画",无 canvas/图片资源):
//   - 昼夜主题:任务里每小时核对,19 点~次日 6 点切夜景(ui_pixel 主题系统);
//   - 天气卡左侧画像素天气图标(晴/少云/阴/雾/雨/雪/雷),拉取中显示四点 spinner;
//   - 底部白字加墨色错位硬阴影,压在草地上也清晰。
//
// 2026-09-13 晚 · 首页版式重构(上中下三层,用户定稿):
//   顶部:像素块大时钟(5×7 字模拼块,真·像素粗体,直接画在天空上)+ 日期行;
//   中部:右侧 天气图标/短语 + 城市 + 温度 + 湿度·风级一行
//        (原两张统计卡降维合并进这一行);
//   底部:名牌卡 = 姓名(20px 标题字) + 个性签名,浮在天空上,草地只留按键提示。
//
// 2026-11-22 · 四柱历与每日一句:
//   左卡:离线四柱(年/月/日/时) + 农历日期、当日交节与下一节气;
//   右列(120 宽):点阵时钟卡(5×7 字模,每格画 4×4 光点) / 日期+城市+Wi-Fi 卡 /
//                天气卡(单色图标 + 温度 + 短语 + 分隔线 + 风向·风级);
//   底部:联网获取并缓存的每日一句;外部文本只显示,不执行其中内容。
#include "apps/home/app_home.h"

#include "apps/home/home_almanac.h"
#include "apps/home/home_clock.h"
#include "apps/home/home_daily_quote.h"
#include "apps/home/home_profile.h"
#include "apps/home/home_upload.h"
#include "apps/home/home_weather.h"
#include "apps/voice/time_sync.h"
#include "launcher/app_registry.h"
#include "launcher/settings.h"
#include "bsp_display.h"
#include "fonts/app_fonts.h"   // buddy_font_16:首页中文动态文本全靠它
#include "ui_pixel.h"
#include "ui/pixel_scene.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

extern "C" {
#include "bsp_battery.h"
}

#include <algorithm>
#include <ctime>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

constexpr char kTag[] = "app_home";

// Playful supplies a dark variant for Home's artwork; Paper intentionally
// keeps its warm light surface.
#define kBg (ui_theme_color_on_dark(UI_COLOR_BG))
#define kPhos (ui_theme_color_on_dark(UI_COLOR_TEXT))
#define kPhosDim (ui_theme_color_on_dark(UI_COLOR_TEXT_DIM))
#define kPhosBorder (ui_theme_color_on_dark(UI_COLOR_BORDER))
#define kClock ((ui_theme_get() == UI_THEME_NEON || ui_theme_get() == UI_THEME_NIGHT) ? UI_ACCENT : ui_theme_get() == UI_THEME_PLAYFUL ? ui_theme_color_on_dark(UI_COLOR_SECONDARY) : kPhos)
#define kTemperature (ui_theme_get() == UI_THEME_NIGHT ? UI_ACCENT : ui_theme_get() == UI_THEME_PAPER ? kPhos : ui_theme_is_multicolor() ? ui_theme_color_on_dark(UI_COLOR_TERTIARY) : kPhos)
#define kWeatherAccent (ui_theme_color_on_dark((ui_theme_get() == UI_THEME_NEON || ui_theme_get() == UI_THEME_NIGHT) ? UI_COLOR_SUCCESS : ui_theme_get() == UI_THEME_PAPER ? UI_COLOR_SUCCESS : UI_COLOR_ACCENT))

// 兼容既有更新函数的语义色:磷光风格里"强调/在线"都是亮绿,弱化都是暗绿。
#define kText (kPhos)
#define kSub (kPhosDim)
#define kMuted (kPhosDim)
#define kAccent (UI_ACCENT)
#define kOnline (kPhos)
#define kOffline (kPhosDim)

// 版式栅格:所有元素都挂在这几个常量上,改一个边距整屏跟着走,不再散落硬编码。
constexpr int kMargin = 12;
constexpr int kContentW = 240 - kMargin * 2;  // 216
constexpr int kInfoLeft = 130; // Visible left edge of the compressed information rail.
constexpr int kClockSlotOffset[5] = {0, 25, 50, 56, 81};

constexpr int64_t kWeatherFirstDelayMs = 3000;
// 亮屏时天气每 15 分钟自动刷新(Open-Meteo 当前值本身按 15 分钟更新);
// 空气质量模型更新慢,保持 30 分钟,不增加 TLS 请求次数。
constexpr int64_t kWeatherPeriodMs = 900000;
constexpr int64_t kAirPeriodMs = 1800000;
// 失败(信号差/热点刚连上)后 1 分钟再试,而不是干等 30 分钟。
constexpr int64_t kWeatherRetryMs = 60000;
// 超过 2 小时的天气在风级行改为显示"几小时前更新",户外断网时不把旧数据当实时。
constexpr int64_t kWeatherStaleSec = 7200;
constexpr int64_t kUploadTimeoutMs = 600000;

// Only the Home worker owns this state. Keep deadlines across app visits so a
// fresh sample does not cause another pair of TLS requests on every return.
struct WeatherSchedule {
    static constexpr unsigned Weather = 1, Air = 2, Both = Weather | Air;
    int64_t weather_due_ms = -1, air_due_ms = -1;
    char lat[HOME_COORD_MAX]{}, lon[HOME_COORD_MAX]{};

    unsigned due(int64_t now, int64_t entered, bool force, const home_profile_t &profile) const {
        if (!profile.lat[0] || !profile.lon[0]) return 0;
        if (force || (lat[0] && (std::strcmp(lat, profile.lat) || std::strcmp(lon, profile.lon)))) return Both;
        if (weather_due_ms < 0 && air_due_ms < 0) return now - entered >= kWeatherFirstDelayMs ? Both : 0;
        return (weather_due_ms <= now ? Weather : 0u) | (air_due_ms <= now ? Air : 0u);
    }

    void begin(unsigned requests, int64_t now, const home_profile_t &profile) {
        std::snprintf(lat, sizeof(lat), "%s", profile.lat);
        std::snprintf(lon, sizeof(lon), "%s", profile.lon);
        // An interrupted request stays due when the next Home worker starts.
        if (requests & Weather) weather_due_ms = now;
        if (requests & Air) air_due_ms = now;
    }

    void finish(unsigned request, int64_t now, esp_err_t result) {
        const int64_t next = now + (request == Air ? kAirPeriodMs
                                    : result != ESP_OK ? kWeatherRetryMs : kWeatherPeriodMs);
        if (request == Weather) weather_due_ms = next;
        else air_due_ms = next;
    }
};
WeatherSchedule s_weather_schedule;

std::atomic<bool> s_home_active{false};
// 界面存活开关:home_task 每轮持锁刷新前检查,stop() 删屏前置 false。
// 解决"stop 超时后任务仍活着却去写已删除 label"这一类的 use-after-free。
std::atomic<bool> s_ui_alive{false};
std::atomic<bool> s_force_weather{false};
int64_t s_manual_weather_ms = -30000;
TaskHandle_t s_task_handle = nullptr;
std::atomic<bool> s_worker_running{false};
uint8_t s_detail_mode = 0;

lv_obj_t *s_screen = nullptr;
lv_obj_t *s_lbl_date = nullptr;
lv_obj_t *s_wifi_bars[3] = {nullptr, nullptr, nullptr};
ui_pixel_battery_t s_batt = {nullptr, nullptr};
// 四柱按三个严格对齐的行绘制:列头、天干、地支。
lv_obj_t *s_pillar_labels[3][4] = {};
lv_obj_t *s_lbl_lunar_date = nullptr;
lv_obj_t *s_lbl_solar_term_today = nullptr;
lv_obj_t *s_lbl_next_solar_term = nullptr;
int64_t s_almanac_key = -1;

lv_obj_t *s_lbl_quote = nullptr;
uint32_t s_quote_version = 0;

// ---- 像素点阵时钟:4 个数字槽 + 冒号槽(槽 2),5×7 字模,每格一个 4×4 光点 ----
lv_obj_t *s_clk_slots[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
lv_obj_t *s_clk_colon[2] = {nullptr, nullptr};   // 冒号两点,每秒呼吸
char s_clk_slot_char[5] = {0, 0, 0, 0, 0};       // 各槽当前字符,变化才重绘
bool s_clk_colon_on = false;

lv_obj_t *s_wicon_zone = nullptr;    // Weather glyph: 22×22, drawn without child objects.
lv_obj_t *s_lbl_city = nullptr;
lv_obj_t *s_lbl_wtext = nullptr;
lv_obj_t *s_lbl_temp = nullptr;
lv_obj_t *s_lbl_humidity = nullptr;  // 湿度 (网格第 1 行左)
lv_obj_t *s_lbl_wind = nullptr;      // 风向 (网格第 1 行右)
lv_obj_t *s_lbl_feels = nullptr;     // 体感 (网格第 2 行左)
lv_obj_t *s_lbl_stat = nullptr;      // 状态 (网格第 2 行右)
lv_obj_t *s_lbl_footer = nullptr;

// Weather artwork is drawn in the existing zone; no retained blocks or bitmap.
uint16_t s_wicon_code = 0;
bool s_wicon_visible = false;
uint8_t s_spin_phase = 0;
bool s_wicon_spinning = false;
bool s_weather_fetching = false;

// ---- 天气像素图标:WMO 码 → 方块拼画,与整机"积木"图标语言同源 ----
void update_weather_labels(const home_profile_t *prof);  // 定义在本段之后

lv_obj_t *px_container(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    return o;
}

lv_obj_t *px_block(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    return o;
}

// Stepped contours share a one-pixel edge and two-pixel accents at native size.
void wicon_cloud(pixel_scene::Ink &ink, int x, int y) {
    const uint32_t edge = kPhos;
    ink.box(x + 8, y, 5, 1, edge);
    ink.box(x + 6, y + 1, 2, 2, edge);
    ink.box(x + 13, y + 1, 2, 2, edge);
    ink.box(x + 3, y + 3, 4, 1, edge);
    ink.box(x + 15, y + 3, 2, 1, edge);
    ink.box(x + 1, y + 4, 2, 1, edge);
    ink.box(x + 17, y + 4, 2, 1, edge);
    ink.box(x, y + 5, 1, 4, edge);
    ink.box(x + 19, y + 5, 1, 4, edge);
    ink.box(x + 1, y + 9, 2, 1, edge);
    ink.box(x + 17, y + 9, 2, 1, edge);
    ink.box(x + 3, y + 10, 14, 1, edge);
    ink.box(x + 4, y + 8, 12, 1, kPhosDim);
}

void draw_weather_icon_event(lv_event_t *event) {
    if (!s_wicon_visible) return;
    pixel_scene::Ink ink(event);
    if (s_wicon_spinning) {
        static constexpr int dots[4][2] = {{10, 3}, {16, 9}, {10, 15}, {4, 9}};
        for (int i = 0; i < 4; ++i)
            ink.box(dots[i][0], dots[i][1], 3, 3, i == s_spin_phase ? kPhos : kPhosDim);
        return;
    }
    const uint32_t sun = ui_theme_is_multicolor()
        ? ui_theme_color_on_dark(UI_COLOR_WARNING) : kPhos;
    const uint32_t water = ui_theme_is_multicolor()
        ? ui_theme_color_on_dark(UI_COLOR_SECONDARY) : kPhos;
    const uint16_t code = s_wicon_code;
    if (code == 0) {
        // Open solar disc with eight short, balanced rays.
        ink.box(8, 6, 6, 2, sun); ink.box(8, 14, 6, 2, sun);
        ink.box(6, 8, 2, 6, sun); ink.box(14, 8, 2, 6, sun);
        ink.box(10, 1, 2, 3, sun); ink.box(10, 18, 2, 3, sun);
        ink.box(1, 10, 3, 2, sun); ink.box(18, 10, 3, 2, sun);
        ink.box(3, 3, 2, 2, sun); ink.box(17, 3, 2, 2, sun);
        ink.box(3, 17, 2, 2, sun); ink.box(17, 17, 2, 2, sun);
        return;
    }
    if (code == 1 || code == 2) {
        // The sun peeks above the cloud; keep the foreground outline clear.
        ink.box(14, 3, 4, 1, sun); ink.box(13, 4, 1, 2, sun);
        ink.box(18, 4, 1, 3, sun); ink.box(16, 0, 1, 2, sun);
        ink.box(20, 2, 2, 1, sun); ink.box(20, 6, 2, 1, sun);
        wicon_cloud(ink, 1, 8);
        return;
    }
    const bool fog = code == 45 || code == 48;
    const bool rain = (code >= 51 && code <= 67) || (code >= 80 && code <= 82);
    const bool snow = (code >= 71 && code <= 77) || code == 85 || code == 86;
    const bool thunder = code >= 95;
    wicon_cloud(ink, 1, fog || rain || snow || thunder ? 2 : 5);
    if (fog) {
        ink.box(3, 16, 11, 1, kPhosDim); ink.box(16, 16, 3, 1, kPhosDim);
        ink.box(5, 19, 13, 1, kPhosDim);
    } else if (rain) {
        for (int x : {5, 11, 17}) {
            ink.box(x, 16, 1, 2, water); ink.box(x - 1, 18, 1, 2, water);
        }
    } else if (snow) {
        for (int x : {6, 15}) {
            ink.box(x, 16, 1, 5, water); ink.box(x - 2, 18, 5, 1, water);
        }
    } else if (thunder) {
        ink.box(11, 14, 3, 2, sun); ink.box(9, 16, 4, 2, sun);
        ink.box(11, 18, 2, 1, sun); ink.box(10, 19, 2, 2, sun);
    }
}

void wicon_clear(void) {
    s_wicon_visible = false;
    s_wicon_spinning = false;
    if (s_ui_alive.load() && s_wicon_zone) lv_obj_invalidate(s_wicon_zone);
}

void weather_icon_draw(uint16_t code) {
    s_wicon_code = code;
    s_wicon_visible = true;
    s_wicon_spinning = false;
    if (s_wicon_zone) lv_obj_invalidate(s_wicon_zone);
}

void weather_spinner_draw(void) {
    s_spin_phase = 0;
    s_wicon_visible = true;
    s_wicon_spinning = true;
    if (s_wicon_zone) lv_obj_invalidate(s_wicon_zone);
}

void weather_icon_spin_tick(void) {
    if (!s_wicon_spinning || !s_ui_alive.load() || !s_wicon_zone) return;
    s_spin_phase = (s_spin_phase + 1) & 3;
    lv_obj_invalidate(s_wicon_zone);
}

// 天气区整体刷新:文字行 + 左侧图标。调用方需已持 LVGL 锁。
void refresh_weather_ui(void) {
    if (!s_ui_alive.load() || s_screen == nullptr) return;
    home_profile_t prof;
    home_profile_get(&prof);
    update_weather_labels(&prof);
    if (prof.lat[0] == '\0') {
        wicon_clear();                       // 未设城市:只留文字提示
    } else if (s_weather_fetching) {
        weather_spinner_draw();
    } else {
        home_weather_t w;
        if (home_weather_get(&w) && w.valid) {
            weather_icon_draw(w.code);
        } else {
            wicon_clear();                   // 无数据:不画,文字会提示"获取中"
        }
    }
}

// ---- 点阵时钟 ----
// 5×7 字模(行位掩码,bit4=最左列),每个点亮格画一个 4×4 光点(格距 5),
// 呈现 LED 点阵质感,与磷光锁屏参考图一致。不依赖任何字体文件。
constexpr int kCell = 5;             // 格距 → 数字 25×35
constexpr int kDot = 4;              // 光点边长(留 1px 缝出点阵感)
constexpr int kDigitW = 5 * kCell;   // 25
constexpr int kClockH = 7 * kCell;   // 35
constexpr uint8_t kDashGlyphIdx = 10;
const uint8_t kDigitGlyphs[11][7] = {
    {0x1F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1F},  // 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},  // 1
    {0x1F, 0x01, 0x01, 0x1F, 0x10, 0x10, 0x1F},  // 2
    {0x1F, 0x01, 0x01, 0x0F, 0x01, 0x01, 0x1F},  // 3
    {0x11, 0x11, 0x11, 0x1F, 0x01, 0x01, 0x01},  // 4
    {0x1F, 0x10, 0x10, 0x1F, 0x01, 0x01, 0x1F},  // 5
    {0x1F, 0x10, 0x10, 0x1F, 0x11, 0x11, 0x1F},  // 6
    {0x1F, 0x01, 0x02, 0x04, 0x04, 0x04, 0x04},  // 7
    {0x1F, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x1F},  // 8
    {0x1F, 0x11, 0x11, 0x1F, 0x01, 0x01, 0x1F},  // 9
    {0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00},  // '-' 占位
};

// Draw the same pixel glyph directly. A digit no longer owns 10-25 child
// objects, and a minute change no longer frees/reallocates their styles.
void draw_digit_event(lv_event_t *event) {
    auto *slot = static_cast<lv_obj_t *>(lv_event_get_target(event));
    const char c = *static_cast<const char *>(lv_event_get_user_data(event));
    lv_area_t origin;
    lv_obj_get_coords(slot, &origin);
    lv_draw_rect_dsc_t ink;
    lv_draw_rect_dsc_init(&ink);
    ink.bg_color = lv_color_hex(kClock);
    ink.bg_opa = LV_OPA_COVER;
    int idx = kDashGlyphIdx;
    if (c >= '0' && c <= '9') idx = c - '0';
    for (int r = 0; r < 7; ++r) {
        for (int col = 0; col < 5; ++col) {
            if (kDigitGlyphs[idx][r] & (0x10 >> col)) {
                const int x = origin.x1 + col * kCell, y = origin.y1 + r * kCell;
                const lv_area_t dot = {x, y, x + kDot - 1, y + kDot - 1};
                lv_draw_rect(lv_event_get_layer(event), &ink, &dot);
            }
        }
    }
}

// 数字槽重绘 + 日期 + 冒号每秒呼吸(时钟区唯一的逐秒动作,只动 opa 不重排)
void update_clock_labels() {
    char hh[3] = "--";
    char mm[3] = "--";
    char date_buf[24];
    const bool valid = home_clock_valid();
    if (valid) {
        char time_buf[8];
        home_clock_format_time(time_buf, sizeof(time_buf));
        hh[0] = time_buf[0];
        hh[1] = time_buf[1];
        mm[0] = time_buf[3];
        mm[1] = time_buf[4];
        char ymd[16];
        char week[8];
        home_clock_format_date(ymd, sizeof(ymd));
        home_clock_format_weekday(week, sizeof(week));
        std::snprintf(date_buf, sizeof(date_buf), "%s %s", ymd, week);
    } else {
        // 未校时一律显示占位符。时间只能来自 SNTP 或 PC 校时,设备自己没有可信来源。
        std::snprintf(date_buf, sizeof(date_buf), "等待校时");
    }

    // The leading '1' and unsynchronized dash have one empty pixel column.
    // Shift the whole clock as a group so its visible ink starts at kInfoLeft,
    // retaining the existing spacing between digits and colon.
    const int clock_inset = (hh[0] == '1' || hh[0] == '-') ? kCell : 0;
    for (int i = 0; i < 5; ++i) {
        const int x = kInfoLeft + kClockSlotOffset[i] - clock_inset;
        if (s_clk_slots[i] && lv_obj_get_x(s_clk_slots[i]) != x)
            lv_obj_set_x(s_clk_slots[i], x);
    }
    const char digits[4] = {hh[0], hh[1], mm[0], mm[1]};
    const int slot_map[4] = {0, 1, 3, 4};   // 槽 2 是冒号
    for (int i = 0; i < 4; ++i) {
        const int slot = slot_map[i];
        if (s_clk_slots[slot] && s_clk_slot_char[slot] != digits[i]) {
            s_clk_slot_char[slot] = digits[i];
            lv_obj_invalidate(s_clk_slots[slot]);
        }
    }
    ui_pixel_label_set_text(s_lbl_date, date_buf);
    lv_font_glyph_dsc_t first_glyph{};
    const uint32_t first_codepoint = valid ? static_cast<uint8_t>(date_buf[0]) : 0x7b49; // 等
    const int date_inset = lv_font_get_glyph_dsc(&buddy_font_16, &first_glyph, first_codepoint, 0)
        ? first_glyph.ofs_x : 0;
    // The subset glyph for 等 also has one transparent column inside its box.
    lv_obj_set_x(s_lbl_date, kInfoLeft - date_inset - (valid ? 0 : 1));

    s_clk_colon_on = !s_clk_colon_on;
    for (lv_obj_t *dot : s_clk_colon) {
        if (dot) {
            lv_obj_set_style_opa(dot, s_clk_colon_on ? LV_OPA_COVER : LV_OPA_30, 0);
        }
    }
}

// 电量画成一个 24×12 的迷你电池:外框 + 按 SOC 伸缩的内条 + 右侧百分比数字。
// 比单纯的 "78%" 多占了两个对象,但一眼能读出剩余量,是值得的。
// 顶栏电池与 Wi-Fi 信号都用共享像素构件,与 Launcher/AI语音 顶栏一致。
void update_battery_label() {
    ui_pixel_battery_set_on_dark(&s_batt, bsp_battery_soc());

    const bool connected = WifiManager::GetInstance().IsConnected();
    int bars = 0;
    uint32_t color = kOffline;
    if (connected) {
        const int rssi = WifiManager::GetInstance().GetRssi();
        bars = rssi >= -60 ? 3 : (rssi >= -75 ? 2 : 1);
        color = kOnline;
    }
    ui_pixel_wifi_bars_set(s_wifi_bars, bars, color);
}

// 天气拆三段:城市(弱) / 天气短语(次) / 温度(强调色 + 20px 数字)。
// 温度用 montserrat 而不是中文字库,是因为 "24°" 这种字形的数字比中文点阵细腻得多。
void update_weather_labels(const home_profile_t *prof) {
    if (!s_ui_alive.load() || s_lbl_city == nullptr) return;
    char pm_line[32];
    home_air_quality_label(WifiManager::GetInstance().IsConnected(), pm_line, sizeof(pm_line));
    if (s_lbl_stat) lv_label_set_text(s_lbl_stat, pm_line);
    if (prof->lat[0] == '\0') {
        lv_label_set_text(s_lbl_city, "未设城市");
        lv_label_set_text(s_lbl_wtext, "");
        lv_label_set_text(s_lbl_temp, "--");
        if (s_lbl_humidity) lv_label_set_text(s_lbl_humidity, "湿度 --");
        if (s_lbl_feels) lv_label_set_text(s_lbl_feels, "体感 --");
        return;
    }
    home_weather_t w;
    const bool ok = home_weather_get(&w);
    lv_label_set_text(s_lbl_city, prof->city);
    if (ok && w.valid) {
        lv_label_set_text(s_lbl_wtext, home_weather_code_text(w.code));
        char t[12];
        std::snprintf(t, sizeof(t), "%d°", static_cast<int>(w.temp_c));
        lv_label_set_text(s_lbl_temp, t);

        char h[20];
        std::snprintf(h, sizeof(h), "湿度 %u%%", (unsigned)w.humidity);
        if (s_lbl_humidity) lv_label_set_text(s_lbl_humidity, h);

        int feels = static_cast<int>(w.temp_c);
        if (w.temp_c >= 25 && w.humidity > 60) {
            feels += (w.humidity - 60) / 12;
        } else if (w.temp_c < 15 && w.wind_kmh > 15) {
            feels -= (w.wind_kmh - 15) / 10;
        }
        char f[20];
        std::snprintf(f, sizeof(f), "体感 %d°", feels);
        if (s_lbl_feels) lv_label_set_text(s_lbl_feels, f);

    } else {
        lv_label_set_text(s_lbl_wtext, s_weather_fetching ? "获取中" : "暂无");
        lv_label_set_text(s_lbl_temp, "--");
        if (s_lbl_humidity) lv_label_set_text(s_lbl_humidity, "湿度 --");
        if (s_lbl_feels) lv_label_set_text(s_lbl_feels, "体感 --");
    }
}

// 天气卡风级行:风向 + 蒲福风级 (例如:北风 3级)。
void update_wind_line() {
    if (!s_ui_alive.load() || s_lbl_wind == nullptr) return;
    if (s_lbl_stat) {
        char line[32];
        home_air_quality_label(WifiManager::GetInstance().IsConnected(), line, sizeof(line));
        ui_pixel_label_set_text(s_lbl_stat, line);
    }
    home_weather_t w;
    if (home_weather_get(&w) && w.valid) {
        char line[40];
        const int64_t now = static_cast<int64_t>(std::time(nullptr));
        const bool unknown_age = w.updated_at == 0 || !home_clock_valid();
        const bool stale = unknown_age
            ? !WifiManager::GetInstance().IsConnected()
            : now - static_cast<int64_t>(w.updated_at) >= kWeatherStaleSec;
        if (stale) {
            char age[24];
            home_weather_age_text(w.updated_at, unknown_age ? 0 : now, age, sizeof(age));
            if (unknown_age) std::snprintf(line, sizeof(line), "%s", age);
            else std::snprintf(line, sizeof(line), "%s更新", age);
        } else {
            std::snprintf(line, sizeof(line), "%s %d级",
                          home_weather_wind_dir_text(w.wind_dir),
                          home_weather_beaufort(w.wind_kmh));
        }
        ui_pixel_label_set_text(s_lbl_wind, line);
    } else {
        ui_pixel_label_set_text(s_lbl_wind, "风向 --");
    }
}

// 底部一行在"操作提示 / 上传提示 / 详情"之间轮换(下键切换),
// 上传模式优先级最高 —— 那时用户正盯着手机扫码,必须立刻看到 URL。
// 按键提示用暗色退后(与其他页面一致);IP/PM2.5/上传地址等信息行保持亮色。
void footer_set(const char *text, const lv_font_t *font, uint32_t color) {
    if (lv_obj_get_style_text_font(s_lbl_footer, LV_PART_MAIN) != font)
        lv_obj_set_style_text_font(s_lbl_footer, font, 0);
    ui_pixel_text_color(s_lbl_footer, color);
    ui_pixel_label_set_text(s_lbl_footer, text);

}

void update_footer_label() {
    if (home_upload_active()) {
        footer_set(home_upload_url(), &lv_font_montserrat_14, kPhos);
        return;
    }

    if (s_detail_mode == 1) {
        footer_set("长按上换句 · 长按下设置", &buddy_font_16, kPhosDim);
        return;
    }
    if (s_detail_mode == 2) {
        if (!WifiManager::GetInstance().IsConnected()) {
            footer_set("未连接无线网络", &buddy_font_16, kPhos);
            return;
        }
        home_weather_t w;
        const bool ok = home_weather_get(&w);
        const std::string ip = WifiManager::GetInstance().GetIpAddress();
        char line[48];
        char age[24] = "暂无天气";
        if (ok && w.valid) {
            home_weather_age_text(w.updated_at,
                                  home_clock_valid() ? static_cast<int64_t>(std::time(nullptr)) : 0,
                                  age, sizeof(age));
        }
        std::snprintf(line, sizeof(line), "%s · 确定刷新", age);
        footer_set(line, &buddy_font_16, kPhos);
        return;
    }
    if (s_detail_mode == 3) {
        home_air_quality_t air;
        if (WifiManager::GetInstance().IsConnected() && home_air_quality_get(&air)) {
            char line[48];
            std::snprintf(line, sizeof(line), "PM2.5 %u.%u ug/m3",
                          (unsigned)(air.pm25_x10 / 10), (unsigned)(air.pm25_x10 % 10));
            footer_set(line, &lv_font_montserrat_14, kPhos);
        } else {
            footer_set("PM2.5 暂无数据", &buddy_font_16, kPhos);
        }
        return;
    }
    if (s_detail_mode == 4) {
        footer_set("Open-Meteo/CAMS 估算", &buddy_font_16, kPhos);
        return;
    }
    footer_set("上干支 · 下名言 · 确定应用", &buddy_font_16, kPhosDim);
}

bool local_calendar(struct tm *out) {
    if (!out || !home_clock_valid()) return false;
    const time_t local = time(nullptr) + static_cast<time_t>(time_sync_tz_offset_sec());
    return gmtime_r(&local, out) != nullptr;
}

bool local_day_key(uint32_t *out) {
    struct tm tm;
    if (!out || !local_calendar(&tm)) return false;
    *out = static_cast<uint32_t>((tm.tm_year + 1900) * 10000 +
                                 (tm.tm_mon + 1) * 100 + tm.tm_mday);
    return true;
}

uint32_t utf8_codepoint(const char **cursor) {
    const auto *p = reinterpret_cast<const uint8_t *>(*cursor);
    if (!p || !*p) return 0;
    if (p[0] < 0x80) {
        *cursor += 1;
        return p[0];
    }
    int bytes = 0;
    uint32_t value = 0;
    if ((p[0] & 0xe0) == 0xc0) {
        bytes = 2;
        value = p[0] & 0x1f;
    } else if ((p[0] & 0xf0) == 0xe0) {
        bytes = 3;
        value = p[0] & 0x0f;
    } else if ((p[0] & 0xf8) == 0xf0) {
        bytes = 4;
        value = p[0] & 0x07;
    } else {
        *cursor += 1;
        return 0xfffd;
    }
    for (int i = 1; i < bytes; ++i) {
        if ((p[i] & 0xc0) != 0x80) {
            *cursor += 1;
            return 0xfffd;
        }
        value = (value << 6) | (p[i] & 0x3f);
    }
    *cursor += bytes;
    return value;
}

void append_utf8(char *out, size_t capacity, size_t *used, uint32_t cp) {
    char encoded[5] = {};
    int bytes = 0;
    if (cp < 0x80) {
        encoded[0] = static_cast<char>(cp);
        bytes = 1;
    } else if (cp < 0x800) {
        encoded[0] = static_cast<char>(0xc0 | (cp >> 6));
        encoded[1] = static_cast<char>(0x80 | (cp & 0x3f));
        bytes = 2;
    } else if (cp < 0x10000) {
        encoded[0] = static_cast<char>(0xe0 | (cp >> 12));
        encoded[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        encoded[2] = static_cast<char>(0x80 | (cp & 0x3f));
        bytes = 3;
    } else {
        encoded[0] = static_cast<char>(0xf0 | (cp >> 18));
        encoded[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
        encoded[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        encoded[3] = static_cast<char>(0x80 | (cp & 0x3f));
        bytes = 4;
    }
    if (*used + static_cast<size_t>(bytes) >= capacity) return;
    std::memcpy(out + *used, encoded, static_cast<size_t>(bytes));
    *used += static_cast<size_t>(bytes);
    out[*used] = '\0';
}

enum class FiveElement { Wood, Fire, Earth, Metal, Water, Unknown };

FiveElement five_element_for_glyph(const char *glyph) {
    if (std::strstr("甲乙寅卯", glyph)) return FiveElement::Wood;
    if (std::strstr("丙丁巳午", glyph)) return FiveElement::Fire;
    if (std::strstr("戊己辰戌丑未", glyph)) return FiveElement::Earth;
    if (std::strstr("庚辛申酉", glyph)) return FiveElement::Metal;
    if (std::strstr("壬癸亥子", glyph)) return FiveElement::Water;
    return FiveElement::Unknown;
}

uint32_t five_element_color(FiveElement element) {
    static constexpr uint32_t kClassic[5] = {
        0x82C89A, 0xE28A72, 0xDDB36C, 0xE2DED0, 0x79B8D8,
    };
    static constexpr uint32_t kNeon[5] = {
        0x5EF29E, 0xFF6B7A, 0xFFD25A, 0xEAF4FF, 0x54C8FF,
    };
    static constexpr uint32_t kPlayful[5] = {
        0x2F7D52, 0xB4463B, 0x8A5A13, 0x4B5563, 0x246B91,
    };
    static constexpr uint32_t kPaper[5] = {
        0x47745A, 0xB43B2D, 0x946528, 0x5B5145, 0x3F6078,
    };
    static constexpr uint32_t kNight[5] = {
        0x72B6A1, 0xCF5B4D, 0xD1A75B, 0xD7D6CB, 0x67A9C4,
    };
    const int index = static_cast<int>(element);
    if (index < 0 || index >= 5) return kPhos;
    const uint32_t *palette = ui_theme_get() == UI_THEME_PLAYFUL ? kPlayful
        : ui_theme_get() == UI_THEME_NEON ? kNeon
        : ui_theme_get() == UI_THEME_PAPER ? kPaper
        : ui_theme_get() == UI_THEME_NIGHT ? kNight : kClassic;
    return palette[index];
}

// Network text is untrusted UI data. Keep only code points present in the
// compact on-device font; unsupported characters become a visible separator.
void font_safe_text(const char *input, char *out, size_t capacity) {
    if (!out || capacity == 0) return;
    out[0] = '\0';
    if (!input) return;
    size_t used = 0;
    const char *cursor = input;
    while (*cursor) {
        uint32_t cp = utf8_codepoint(&cursor);
        if (cp == '\n' || cp == '\r' || cp == '\t') cp = ' ';
        if (cp < 0x20U) continue;
        lv_font_glyph_dsc_t glyph{};
        if (!lv_font_get_glyph_dsc(&buddy_font_16, &glyph, cp, 0)) cp = 0x00b7;
        append_utf8(out, capacity, &used, cp);
    }
}

void update_almanac_labels() {
    if (!s_ui_alive.load() || !s_pillar_labels[0][0]) return;
    struct tm tm;
    if (!local_calendar(&tm)) {
        const int64_t key = -1;
        if (s_almanac_key == key) return;
        s_almanac_key = key;
        for (auto &row : s_pillar_labels) {
            for (lv_obj_t *label : row) lv_label_set_text(label, "--");
        }
        lv_label_set_text(s_lbl_lunar_date, "等待校时");
        lv_label_set_text(s_lbl_solar_term_today, "");
        lv_label_set_text(s_lbl_next_solar_term, "");
        return;
    }

    const home_zi_rule_t zi_rule = settings_get_late_zi() ? HOME_ZI_LATE : HOME_ZI_EARLY;
    const int64_t key = (((((static_cast<int64_t>(tm.tm_year + 1900) * 13 +
                           tm.tm_mon + 1) * 32 + tm.tm_mday) * 24 +
                           tm.tm_hour) * 60 + tm.tm_min) * 2 + zi_rule);
    if (key == s_almanac_key) return;
    s_almanac_key = key;

    home_almanac_t almanac;
    if (!home_almanac_calculate_ex(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                                   tm.tm_hour, tm.tm_min, zi_rule, &almanac)) {
        for (auto &row : s_pillar_labels) {
            for (lv_obj_t *label : row) lv_label_set_text(label, "--");
        }
        lv_label_set_text(s_lbl_lunar_date, "历法超出范围");
        lv_label_set_text(s_lbl_solar_term_today, "");
        lv_label_set_text(s_lbl_next_solar_term, "");
        return;
    }
    const char *headers[] = {"年", "月", "日", "时"};
    const char *pillars[] = {almanac.year, almanac.month, almanac.day, almanac.hour};
    for (int col = 0; col < 4; ++col) {
        char stem[4] = {};
        char branch[4] = {};
        std::snprintf(stem, sizeof(stem), "%.3s", pillars[col]);
        std::snprintf(branch, sizeof(branch), "%.3s", pillars[col] + 3);
        lv_label_set_text(s_pillar_labels[0][col], headers[col]);
        lv_label_set_text(s_pillar_labels[1][col], stem);
        lv_label_set_text(s_pillar_labels[2][col], branch);
        ui_pixel_text_color(s_pillar_labels[1][col],
                            five_element_color(five_element_for_glyph(stem)));
        ui_pixel_text_color(s_pillar_labels[2][col],
                            five_element_color(five_element_for_glyph(branch)));
    }
    lv_label_set_text(s_lbl_lunar_date, almanac.lunar_month_day);
    char term_today[20] = {};
    if (almanac.solar_term_today[0])
        std::snprintf(term_today, sizeof(term_today), "今日交%s",
                      almanac.solar_term_today);
    lv_label_set_text(s_lbl_solar_term_today, term_today);
    char next_term[24];
    std::snprintf(next_term, sizeof(next_term), "距%s %u天",
                  almanac.next_solar_term,
                  (unsigned)almanac.days_to_next_solar_term);
    lv_label_set_text(s_lbl_next_solar_term, next_term);
}

void update_daily_quote_labels() {
    if (!s_ui_alive.load() || !s_lbl_quote) return;
    home_daily_quote_t quote;
    if (home_daily_quote_get(&quote)) {
        char safe[HOME_DAILY_QUOTE_TEXT_MAX * 2];
        font_safe_text(quote.text, safe, sizeof(safe));
        lv_label_set_text(s_lbl_quote, safe);
        int width = 0;
        int lines = 1;
        const char *cursor = safe;
        while (*cursor) {
            const uint32_t cp = utf8_codepoint(&cursor);
            const char *peek = cursor;
            const uint32_t next = *peek ? utf8_codepoint(&peek) : 0;
            const int glyph_width = lv_font_get_glyph_width(&buddy_font_16, cp, next);
            if (width > 0 && width + glyph_width > 202) {
                ++lines;
                width = 0;
            }
            width += glyph_width;
        }
        lv_obj_set_pos(s_lbl_quote, 10, lines == 1 ? 15 : 1);
    } else {
        lv_label_set_text(s_lbl_quote, "");
    }
}

void apply_profile(const home_profile_t *prof) {
    (void)prof;
    refresh_weather_ui();
}

lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                     lv_text_align_t align) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
    return label;
}

// Passport paper details are drawn on demand without retained child objects.
void draw_passport_details(lv_event_t *event) {
    pixel_scene::Ink ink(event);
    // Keep only the right information rail; the left rail is now the almanac.
    const uint32_t surface = ui_theme_color_on_dark(UI_COLOR_SURFACE);
    ink.box(kInfoLeft - 2, 30, 240 - (kInfoLeft - 2), 202, surface);
    // 9x12 outlined map pin: stepped crown, open center and tapered tip.
    // Center y=110.5 matches the visible city glyphs (y=104..117).
    // Draw with the page, avoiding two retained decoration objects.
    ink.box(kInfoLeft + 2, 105, 5, 1, kPhos);
    ink.box(kInfoLeft + 1, 106, 1, 1, kPhos); ink.box(kInfoLeft + 7, 106, 1, 1, kPhos);
    ink.box(kInfoLeft + 0, 107, 1, 4, kPhos); ink.box(kInfoLeft + 8, 107, 1, 4, kPhos);
    ink.box(kInfoLeft + 3, 108, 3, 2, kPhosDim);
    ink.box(kInfoLeft + 1, 111, 1, 1, kPhos); ink.box(kInfoLeft + 7, 111, 1, 1, kPhos);
    ink.box(kInfoLeft + 2, 112, 1, 1, kPhos); ink.box(kInfoLeft + 6, 112, 1, 1, kPhos);
    ink.box(kInfoLeft + 3, 113, 1, 1, kPhos); ink.box(kInfoLeft + 5, 113, 1, 1, kPhos);
    ink.box(kInfoLeft + 4, 114, 1, 3, kPhos);
    for (int y = 34; y < 234; y += 8) ink.box(120, y, 1, 3, kPhosBorder);
    ink.box(kInfoLeft, 162, 108, 1, kPhosBorder);
}

void create_ui() {
    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(kBg), 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);

    // ---- 状态栏 (y0..24):Wi-Fi 信号 + 电池,回到最顶部 ----
    lv_obj_t *top = ui_pixel_top_bar(s_screen);
    if (ui_theme_get() == UI_THEME_PLAYFUL) {
        ui_pixel_bg_color(top, kBg);
        lv_obj_set_style_border_color(top, lv_color_hex(kPhosBorder), 0);
    }
    ui_pixel_wifi_bars_create(s_screen, UI_STATUS_WIFI_X, 18, s_wifi_bars);
    s_batt = ui_pixel_battery_create(s_screen, UI_STATUS_BATTERY_X, 6, kPhos, &lv_font_montserrat_14);

    lv_obj_t *home_title = ui_pixel_label(s_screen, "命海拾遗", &buddy_font_16, kPhosDim);
    lv_obj_set_pos(home_title, 42, 4);

    lv_obj_add_event_cb(s_screen, draw_passport_details, LV_EVENT_DRAW_MAIN, nullptr);

    // ---- 左侧四柱历 (4,30..128,232):继续加宽，右侧保留 112px ----
    lv_obj_t *almanac = px_container(s_screen, 4, 30, 124, 202);
    lv_obj_set_style_bg_color(almanac, lv_color_hex(ui_theme_color_on_dark(UI_COLOR_SURFACE)), 0);
    lv_obj_set_style_bg_opa(almanac, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(almanac, 1, 0);
    lv_obj_set_style_border_color(almanac, lv_color_hex(kPhosBorder), 0);
    lv_obj_set_style_radius(almanac, 0, 0);

    constexpr int kPillarX[4] = {8, 36, 64, 92};
    constexpr int kPillarY[3] = {6, 31, 65};
    for (int col = 0; col < 4; ++col) {
        s_pillar_labels[0][col] = make_label(almanac, &buddy_font_16, kPhosDim,
                                              LV_TEXT_ALIGN_CENTER);
        lv_obj_set_pos(s_pillar_labels[0][col], kPillarX[col], kPillarY[0]);
        lv_obj_set_size(s_pillar_labels[0][col], 24, 18);

        s_pillar_labels[1][col] = make_label(almanac, &ganzhi_font_20, kPhos,
                                              LV_TEXT_ALIGN_CENTER);
        lv_obj_set_pos(s_pillar_labels[1][col], kPillarX[col], kPillarY[1]);
        lv_obj_set_size(s_pillar_labels[1][col], 24, 25);
        lv_label_set_long_mode(s_pillar_labels[1][col], LV_LABEL_LONG_MODE_CLIP);

        s_pillar_labels[2][col] = make_label(almanac, &ganzhi_font_20, kAccent,
                                              LV_TEXT_ALIGN_CENTER);
        lv_obj_set_pos(s_pillar_labels[2][col], kPillarX[col], kPillarY[2]);
        lv_obj_set_size(s_pillar_labels[2][col], 24, 25);
        lv_label_set_long_mode(s_pillar_labels[2][col], LV_LABEL_LONG_MODE_CLIP);
    }
    px_block(almanac, 6, 29, 112, 1, kPhosBorder);
    px_block(almanac, 6, 61, 112, 1, kPhosBorder);
    px_block(almanac, 6, 94, 112, 1, kPhosBorder);

    lv_obj_t *lunar_kicker = make_label(almanac, &buddy_font_16, kPhosDim,
                                         LV_TEXT_ALIGN_CENTER);
    lv_obj_set_pos(lunar_kicker, 0, 98);
    lv_obj_set_size(lunar_kicker, 122, 18);
    lv_label_set_text(lunar_kicker, "农历");

    s_lbl_lunar_date = make_label(almanac, &buddy_font_16, kPhos, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_pos(s_lbl_lunar_date, 0, 117);
    lv_obj_set_size(s_lbl_lunar_date, 122, 18);
    lv_label_set_long_mode(s_lbl_lunar_date, LV_LABEL_LONG_MODE_DOTS);

    s_lbl_solar_term_today = make_label(almanac, &buddy_font_16, kPhos, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_pos(s_lbl_solar_term_today, 0, 143);
    lv_obj_set_size(s_lbl_solar_term_today, 122, 18);

    s_lbl_next_solar_term = make_label(almanac, &buddy_font_16, kPhosDim, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_pos(s_lbl_next_solar_term, 0, 169);
    lv_obj_set_size(s_lbl_next_solar_term, 122, 18);

    // Clock sits 14px below the status bar; date and city form a compact group.
    for (int i = 0; i < 5; ++i) {
        s_clk_slots[i] = px_container(s_screen, kInfoLeft + kClockSlotOffset[i], 40,
                                      i == 2 ? 4 : kDigitW, kClockH);
        if (i != 2) lv_obj_add_event_cb(s_clk_slots[i], draw_digit_event,
                                       LV_EVENT_DRAW_MAIN, &s_clk_slot_char[i]);
    }
    s_clk_colon[0] = px_block(s_clk_slots[2], 0, 7, 4, 4, kClock);
    s_clk_colon[1] = px_block(s_clk_slots[2], 0, 21, 4, 4, kClock);

    // Keep 6px between the 35px clock and its date line.
    s_lbl_date = make_label(s_screen, &buddy_font_16, kPhos, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_width(s_lbl_date, 108);
    lv_obj_set_pos(s_lbl_date, kInfoLeft, 81);

    // Four clear pixels separate the outlined pin from the city name.
    s_lbl_city = make_label(s_screen, &buddy_font_16, kPhosDim, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(s_lbl_city, 143, 102);
    lv_obj_set_size(s_lbl_city, 95, 20);
    lv_label_set_long_mode(s_lbl_city, LV_LABEL_LONG_MODE_DOTS);

    // ---- 右下环境气象区 (x114..234, y124..236: 经典三行大字版式) ----
    // 行 1: 天气图标 + 大温度 + 天气状态
    s_wicon_zone = px_container(s_screen, 128, 135, 22, 22);
    lv_obj_add_event_cb(s_wicon_zone, draw_weather_icon_event, LV_EVENT_DRAW_MAIN, nullptr);
    s_lbl_temp = make_label(s_screen, &lv_font_montserrat_20, kTemperature, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(s_lbl_temp, 154, 134);
    lv_obj_set_size(s_lbl_temp, 32, 24);
    lv_label_set_long_mode(s_lbl_temp, LV_LABEL_LONG_MODE_CLIP);

    s_lbl_wtext = make_label(s_screen, &buddy_font_16, kWeatherAccent, LV_TEXT_ALIGN_LEFT);
    // Three full-width Chinese glyphs need 48px even without tracking.
    // Bound the height as well: DOTS on a content-height label can still wrap.
    lv_obj_set_pos(s_lbl_wtext, 188, 137);
    lv_obj_set_size(s_lbl_wtext, 48, 20);
    lv_obj_set_style_text_letter_space(s_lbl_wtext, 0, 0);
    lv_label_set_long_mode(s_lbl_wtext, LV_LABEL_LONG_MODE_DOTS);

    // 三行垂直清晰气象指标 (y=170, 192, 214), 采用 16px buddy_font_16，舒适清晰
    s_lbl_humidity = make_label(s_screen, &buddy_font_16, kPhosDim, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(s_lbl_humidity, 130, 170);
    lv_obj_set_size(s_lbl_humidity, 108, 20);
    lv_label_set_long_mode(s_lbl_humidity, LV_LABEL_LONG_MODE_DOTS);

    s_lbl_wind = make_label(s_screen, &buddy_font_16, kPhosDim, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(s_lbl_wind, 130, 192);
    lv_obj_set_size(s_lbl_wind, 112, 20);
    lv_label_set_long_mode(s_lbl_wind, LV_LABEL_LONG_MODE_DOTS);

    s_lbl_stat = make_label(s_screen, &buddy_font_16, kPhosDim, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(s_lbl_stat, 130, 214);
    lv_obj_set_size(s_lbl_stat, 112, 20);
    lv_label_set_long_mode(s_lbl_stat, LV_LABEL_LONG_MODE_DOTS);

    s_lbl_feels = nullptr;

    lv_label_set_text(s_lbl_temp, "--");
    lv_label_set_text(s_lbl_wtext, "");
    lv_label_set_text(s_lbl_humidity, "湿度 --");
    lv_label_set_text(s_lbl_stat, "PM2.5 --");
    ui_pixel_label_set_text(s_lbl_wind, "风向 --");

    // ---- 每日一句 (y238..290):两行正文 + 来源/网络提示 ----
    lv_obj_t *quote_zone = px_container(s_screen, 8, 238, 224, 52);
    lv_obj_set_style_bg_color(quote_zone, lv_color_hex(ui_theme_color_on_dark(UI_COLOR_SURFACE)), 0);
    lv_obj_set_style_bg_opa(quote_zone, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(quote_zone, 1, 0);
    lv_obj_set_style_border_color(quote_zone, lv_color_hex(kPhosBorder), 0);
    lv_obj_set_style_radius(quote_zone, 0, 0);
    px_block(quote_zone, 0, 0, 2, 52, kAccent);

    s_lbl_quote = make_label(quote_zone, &buddy_font_16, kPhos, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_pos(s_lbl_quote, 10, 1);
    lv_obj_set_size(s_lbl_quote, 202, 50);
    lv_label_set_long_mode(s_lbl_quote, LV_LABEL_LONG_MODE_DOTS);

    // ---- 按键提示:底黑条 + 1px 细分隔线 + 提拉 6px 安全边距 ----
    px_block(s_screen, 0, 292, 240, 1, kPhosBorder);
    lv_obj_t *strip = px_block(s_screen, 0, 293, 240, 27, kBg);
    lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, 0);
    s_lbl_footer = make_label(s_screen, &buddy_font_16, kPhosDim, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_lbl_footer, kContentW);
    lv_obj_align(s_lbl_footer, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_label_set_long_mode(s_lbl_footer, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(s_lbl_footer, "上干支 · 下名言 · 确定应用");
}

void home_task(void *arg) {
    (void)arg;
    const int64_t start_ms = esp_timer_get_time() / 1000;
    int64_t upload_started_ms = 0;
    bool upload_ps_high = false; // 上传模式是否已切 BALANCED 省电档
    bool clock_on = false;
    uint32_t last_profile_ver = home_profile_version();
    home_profile_t prof;
    home_profile_get(&prof);

    while (s_home_active.load()) {
        const int64_t now_ms = esp_timer_get_time() / 1000;

        if (bsp_lvgl_lock(1000)) {
            if (s_ui_alive.load() && app_registry_screen_is_on()) {
                update_clock_labels();
                update_almanac_labels();
                update_battery_label();
                update_wind_line();
                const uint32_t quote_version = home_daily_quote_version();
                if (quote_version != s_quote_version) {
                    s_quote_version = quote_version;
                    update_daily_quote_labels();
                }
                // 底部行每秒重算:上传模式下它会把刚拿到的 IP 补进 URL,
                // 避免"Wi-Fi 刚连上还没拿到地址"时显示成空的 http://。
                update_footer_label();
                weather_icon_spin_tick();
            }
            bsp_lvgl_unlock();
        }

        // Wi-Fi 就绪才跑 SNTP;断开即停,避免 lwIP 一直重试。
        const bool connected = WifiManager::GetInstance().IsConnected();
        if (connected && !clock_on) {
            home_clock_start();
            clock_on = true;
        } else if (!connected && clock_on) {
            home_clock_stop();
            clock_on = false;
        }

        // 资料变化只影响右侧城市/天气;姓名、签名和人像仍由手机设置页维护。
        const uint32_t profile_ver = home_profile_version();
        if (profile_ver != last_profile_ver) {
            last_profile_ver = profile_ver;
            char previous_lat[HOME_COORD_MAX], previous_lon[HOME_COORD_MAX];
            std::snprintf(previous_lat, sizeof(previous_lat), "%s", prof.lat);
            std::snprintf(previous_lon, sizeof(previous_lon), "%s", prof.lon);
            home_profile_get(&prof);
            if (std::strcmp(previous_lat, prof.lat) || std::strcmp(previous_lon, prof.lon))
                s_force_weather.store(true);
            if (bsp_lvgl_lock(1000)) {
                if (s_ui_alive.load()) apply_profile(&prof);
                bsp_lvgl_unlock();
            }
        }

        // 首次进入延迟 15s;跨页面复用样本,到期自动刷新,手动刷新或换城市立即请求。
        // 熄屏期间不发请求(省电);亮屏后若已到期,下一轮循环立即刷新。
        home_profile_get(&prof);
        // A quote request is short and uses its own task. Defer weather while it
        // is in flight so the two HTTP clients never pressure the small C3 heap.
        const unsigned requests = connected && !home_daily_quote_busy() &&
                                  app_registry_screen_is_on()
            ? s_weather_schedule.due(now_ms, start_ms, s_force_weather.load(), prof) : 0;
        if (requests) {
            s_force_weather.store(false);
            s_weather_schedule.begin(requests, now_ms, prof);
            // 拉取期间天气卡左侧转四点 spinner(每秒转一格),结束后按结果换图标
            s_weather_fetching = true;
            if (bsp_lvgl_lock(1000)) {
                refresh_weather_ui();
                bsp_lvgl_unlock();
            }
            if (requests & WeatherSchedule::Weather) {
                const auto result = home_weather_refresh(prof.lat, prof.lon); // 阻塞数秒,不持锁
                if (s_home_active.load()) s_weather_schedule.finish(WeatherSchedule::Weather, esp_timer_get_time() / 1000, result);
            }
            if (s_home_active.load() && (requests & WeatherSchedule::Air)) {
                const auto result = home_air_quality_refresh(prof.lat, prof.lon);
                if (s_home_active.load()) s_weather_schedule.finish(WeatherSchedule::Air, esp_timer_get_time() / 1000, result);
            }
            s_weather_fetching = false;
            if (!s_home_active.load()) break;
            // 无论成败都要刷新一次:成功换天气图标,失败把 spinner 撤下来
            if (bsp_lvgl_lock(1000)) {
                refresh_weather_ui();
                bsp_lvgl_unlock();
            }
        }

        // Fetch at most once per local day. The worker writes NVS outside LVGL;
        // a failed request leaves yesterday's quote visible and retries later.
        uint32_t quote_day = 0;
        if (connected && !s_weather_fetching && local_day_key(&quote_day))
            home_daily_quote_schedule(quote_day);

        // 上传模式期间恢复即时下行(手机/设备往返都要快),结束时回到待机省电档。
        // 用电平变化检测,按键开启与 10 分钟自动超时两条路径都覆盖。
        const bool upload_active = home_upload_active();
        if (upload_active != upload_ps_high) {
            upload_ps_high = upload_active;
            WifiManager::GetInstance().SetPowerSaveLevel(
                upload_active ? WifiPowerSaveLevel::BALANCED : WifiPowerSaveLevel::LOW_POWER);
        }

        // 上传模式 10 分钟自动关闭,避免 httpd 长期占着内存。
        if (home_upload_active()) {
            if (upload_started_ms == 0) {
                upload_started_ms = now_ms;
            } else if (now_ms - upload_started_ms >= kUploadTimeoutMs) {
                home_upload_stop();
                upload_started_ms = 0;
            }
        } else {
            upload_started_ms = 0;
        }

        // 1s 周期拆成 10×100ms:stop() 后 ≤100ms 内退出任务,应用切换不再
        // 等满一秒 —— 切入小智/收音机时的过渡黑屏时间显著缩短。
        for (int i = 0; i < 10 && s_home_active.load(); ++i) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    s_task_handle = nullptr;
    s_worker_running.store(false);
    vTaskDelete(NULL);
}

void home_init() {
    home_profile_init();
    home_weather_init();
    home_daily_quote_init();
    // 只有 voice 应用调过 time_sync_init();首页自己调一次保证时区已载入(幂等 NVS 读)。
    time_sync_init();

    ESP_LOGI(kTag, "Home app subsystems ready");
}

void home_start() {
    ESP_LOGI(kTag, "Starting Home app");
    home_weather_allow_requests();
    s_home_active.store(true);
    s_ui_alive.store(true);
    s_detail_mode = 0;

    home_profile_t prof;
    home_profile_get(&prof);

    bsp_lvgl_lock(-1);
    if (!s_screen) create_ui();
    apply_profile(&prof);
    update_clock_labels();
    s_almanac_key = -1;
    update_almanac_labels();
    s_quote_version = home_daily_quote_version();
    update_daily_quote_labels();
    update_battery_label();
    update_wind_line();
    update_footer_label();
    lv_scr_load(s_screen);
    bsp_lvgl_unlock();

    // 栈给 8192：本任务内会跑 HTTPS，mbedTLS 握手调用链较深。
    s_worker_running.store(true);
    BaseType_t created = xTaskCreate(home_task, "home_app", 8192, NULL, 4, &s_task_handle);
    if (created != pdPASS) {
        s_worker_running.store(false);
        s_task_handle = nullptr;
        ESP_LOGE(kTag, "Failed to create home task; free heap: %u KB",
                 (unsigned)(esp_get_free_heap_size() / 1024));
        return;
    }
    ESP_LOGI(kTag, "Home app started; free heap: %u KB",
             (unsigned)(esp_get_free_heap_size() / 1024));
}

bool home_prepare_stop() {
    s_home_active.store(false);
    s_ui_alive.store(false);
    home_weather_cancel_requests();
    home_daily_quote_cancel_requests();
    return !s_worker_running.load() && !home_daily_quote_busy();
}

void home_stop() {
    ESP_LOGI(kTag, "Stopping Home app");
    s_home_active.store(false);
    home_weather_cancel_requests();
    home_daily_quote_cancel_requests();
    // 先撤掉 UI 开关:home_task 持锁刷新前会看它,避免"删屏后任务还在写标签"。
    s_ui_alive.store(false);

    // Registry calls this only after prepare_stop reports completion. Never
    // delete a task inside DNS/TLS: it may still own heap or socket locks.
    if (!home_prepare_stop()) return;

    home_upload_stop();
    home_clock_stop();
    home_daily_quote_stop();

    bsp_lvgl_lock(-1);
    if (s_screen) {
        lv_obj_delete(s_screen);
    }
    s_screen = nullptr;
    s_lbl_date = nullptr;
    for (lv_obj_t *&bar : s_wifi_bars) bar = nullptr;
    s_batt = {nullptr, nullptr};
    for (auto &row : s_pillar_labels) {
        for (lv_obj_t *&label : row) label = nullptr;
    }
    s_lbl_lunar_date = nullptr;
    s_lbl_solar_term_today = nullptr;
    s_lbl_next_solar_term = nullptr;
    s_lbl_quote = nullptr;
    s_almanac_key = -1;
    for (lv_obj_t *&slot : s_clk_slots) slot = nullptr;
    for (lv_obj_t *&dot : s_clk_colon) dot = nullptr;
    std::memset(s_clk_slot_char, 0, sizeof(s_clk_slot_char));
    s_wicon_zone = nullptr;
    s_wicon_visible = false;
    s_wicon_spinning = false;
    s_lbl_city = nullptr;
    s_lbl_wtext = nullptr;
    s_lbl_temp = nullptr;
    s_lbl_humidity = nullptr;
    s_lbl_wind = nullptr;
    s_lbl_feels = nullptr;
    s_lbl_stat = nullptr;
    s_lbl_footer = nullptr;
    bsp_lvgl_unlock();
    ESP_LOGI(kTag, "Home app stopped. Free heap: %u KB",
             (unsigned)(esp_get_free_heap_size() / 1024));
}

void home_on_key(uint8_t btn, app_btn_event_t event, uint16_t mv) {
    (void)mv;

    // 手机设置与首次开机共用同一个热点入口。
    if (btn == BSP_BTN_DOWN && event == BTN_EVT_LONG) {
        app_registry_switch_to(APP_ID_WIFI);
        return;
    }

    if (btn == BSP_BTN_UP && event == BTN_EVT_LONG) {
        uint32_t quote_day = 0;
        bsp_lvgl_lock(-1);
        if (!WifiManager::GetInstance().IsConnected()) {
            footer_set("未连接无线网络", &buddy_font_16, kPhos);
        } else if (local_day_key(&quote_day)) {
            home_daily_quote_request_refresh(quote_day);
            footer_set("正在换一句", &buddy_font_16, kPhos);
        }
        bsp_lvgl_unlock();
        return;
    }

    if (event != BTN_EVT_CLICK) return;

    // OK 短按 = 打开应用板块。
    if (btn == BSP_BTN_OK) {
        if (s_detail_mode == 2) {
            const int64_t now = esp_timer_get_time() / 1000;
            bsp_lvgl_lock(-1);
            if (now - s_manual_weather_ms >= 30000 && WifiManager::GetInstance().IsConnected()) {
                s_manual_weather_ms = now;
                s_force_weather.store(true);
                footer_set("正在更新天气", &buddy_font_16, kPhos);
            } else {
                footer_set("请稍后再试", &buddy_font_16, kPhos);
            }
            bsp_lvgl_unlock();
            return;
        }
        ESP_LOGI(kTag, "Opening app drawer from Home");
        app_registry_switch_to(APP_ID_LAUNCHER);
        return;
    }

    if (btn == BSP_BTN_UP) {
        bsp_lvgl_lock(-1);
        if (WifiManager::GetInstance().IsConnected()) {
            s_force_weather.store(true);
            footer_set("正在更新天气", &buddy_font_16, kPhos);
        } else {
            footer_set("未连接无线网络", &buddy_font_16, kPhosDim);
        }
        bsp_lvgl_unlock();
        return;
    } else if (btn == BSP_BTN_DOWN) {
        // 操作提示、手势、网络/天气、PM2.5 单位及数据来源。
        s_detail_mode = static_cast<uint8_t>((s_detail_mode + 1) % 5);
    } else {
        return;
    }

    bsp_lvgl_lock(-1);
    update_footer_label();
    bsp_lvgl_unlock();
}

}  // namespace

const passport_app_t g_home_app = {
    .id = APP_ID_HOME,
    .name = "首页",
    .en_name = "HOME",
    .desc = "四柱 · 天气 · 每日一句",
    .tag = "直连",
    .theme_color = 0xDDB36C,
    .init = home_init,
    .start = home_start,
    .stop = home_stop,
    .on_key = home_on_key,
    .prepare_stop = home_prepare_stop,
};
