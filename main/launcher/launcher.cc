// Application drawer: shared phosphor palette and pixel icons.
#include "launcher/launcher.h"
#include "launcher/app_registry.h"
#include "wifi_manager.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "ui_pixel.h"
#include "ui/pixel_scene.h"
#include "app_fonts.h"
#include "esp_log.h"
#include "lvgl.h"

#include <cstdio>
#include <initializer_list>

static const char *TAG = "launcher";

namespace {

constexpr int kGridX = 8;
constexpr int kGridY = 43;
constexpr int kTileW = 70;
constexpr int kTileH = 54;
constexpr int kColumnGap = 7;
constexpr int kRowGap = 6;
constexpr int kIconSize = 24;
constexpr size_t kMaxTiles = 12;

// 旅行仪器配色:与首页(app_home.cc)完全同源,风格统一。
#define kBg (UI_BG)          // 屏底炭黑
#define kCardBg (UI_SURFACE)      // 卡片底
#define kPhos (UI_ACCENT)        // 琥珀:图标/标题/选中态
#define kPhosDim (UI_TEXT_DIM)     // 灰米色:副标题/未选中
#define kPhosBorder (UI_BORDER)  // 卡片/底板描边
#define kStatus (ui_theme_is_multicolor() ? UI_TEXT : kPhos)

lv_obj_t *s_screen;
lv_obj_t *s_page_number;
lv_obj_t *s_wifi_bars[3];
ui_pixel_battery_t s_battery;
size_t s_selected_index;

struct AppTile {
    lv_obj_t *card = nullptr;
    lv_obj_t *icon = nullptr;
    lv_obj_t *label = nullptr;
    passport_app_id_t id = APP_ID_COUNT;
    size_t index = 0;
};
AppTile s_tiles[kMaxTiles];
size_t s_tile_count;

const char *app_chinese_name(passport_app_id_t id) {
    // 2026-09-13 修复(D1):名称原本在 launcher 里硬编码一份、在 passport_app_t
    // 里又各写一份,两边会悄悄跑偏。改成**以注册表为唯一来源**,只有没注册进
    // 注册表的内置页(配网/设置)才走兜底表。
    const passport_app_t *app = app_registry_get_by_id(id);
    if (app != nullptr && app->name != nullptr && app->name[0] != '\0') {
        return app->name;
    }
    switch (id) {
        case APP_ID_WIFI: return "无线网络";
        case APP_ID_SETTINGS: return "设置";
        default: return "";
    }
}

// One line-icon vocabulary: a 17x17 grid of single-pixel cells drawn in the app color,
// strokes one cell wide, no fills or second tone. Each glyph is centered on
// the grid, so new apps add one table entry instead of hand-placed strokes.
constexpr int kGlyphCells = 17;
constexpr int kGlyphCell = 1;
constexpr int kGlyphOrigin = (kIconSize - kGlyphCells * kGlyphCell) / 2;
struct IconGlyph {
    passport_app_id_t id;
    const char *rows[kGlyphCells];
};
constexpr IconGlyph kIconGlyphs[] = {
    {APP_ID_XIAOZHI, {  // antenna robot
        ".................",
        ".......###.......",
        "........#........",
        "..#############..",
        "..#...........#..",
        "..#...........#..",
        "###...#...#...###",
        "###...#...#...###",
        "###...........###",
        "..#...........#..",
        "..#....###....#..",
        "..#...........#..",
        "..#############..",
        "........#........",
        ".....#######.....",
        ".................",
        ".................",
    }},
    {APP_ID_RADIO, {  // radio with antenna
        ".................",
        ".........##......",
        ".......##........",
        ".....##..........",
        "...##............",
        "#################",
        "#...............#",
        "#...............#",
        "#.######..#####.#",
        "#.........#...#.#",
        "#.######..#.#.#.#",
        "#.........#...#.#",
        "#.######..#####.#",
        "#...............#",
        "#################",
        ".................",
        ".................",
    }},
    {APP_ID_NETEASE, {  // beamed notes
        ".................",
        ".................",
        ".....###########.",
        ".....###########.",
        ".....#.........#.",
        ".....#.........#.",
        ".....#.........#.",
        ".....#.........#.",
        ".....#.........#.",
        ".....#.........#.",
        ".....#.........#.",
        "..####......####.",
        ".#...#.....#...#.",
        ".#...#.....#...#.",
        "..###.......###..",
        ".................",
        ".................",
    }},
    {APP_ID_POMODORO, {  // tomato clock
        "........#........",
        "......##.##......",
        "......#####......",
        "....##.....##....",
        "...#.........#...",
        "..#.....#.....#..",
        "..#.....#.....#..",
        ".#......#......#.",
        ".#......#......#.",
        ".#......####...#.",
        ".#.............#.",
        ".#.............#.",
        "..#...........#..",
        "..#...........#..",
        "...#.........#...",
        "....##.....##....",
        "......#####......",
    }},
    {APP_ID_STOPWATCH, {  // stopwatch
        ".......###.......",
        "........#.....#..",
        "......#####..#...",
        "....##.....##....",
        "...#.........#...",
        "..#...........#..",
        "..#........#..#..",
        ".#........#....#.",
        ".#.......#.....#.",
        ".#......#......#.",
        ".#.............#.",
        ".#.............#.",
        "..#...........#..",
        "..#...........#..",
        "...#.........#...",
        "....##.....##....",
        "......#####......",
    }},
    {APP_ID_REMINDERS, {  // bell
        ".................",
        ".................",
        "........#........",
        "......#####......",
        ".....#.....#.....",
        "....#.......#....",
        "....#.......#....",
        "....#.......#....",
        "....#.......#....",
        "...#.........#...",
        "...#.........#...",
        "..#...........#..",
        ".###############.",
        ".................",
        ".......###.......",
        ".................",
        ".................",
    }},
    {APP_ID_LISTS, {  // list page
        ".................",
        ".................",
        "..#############..",
        "..#...........#..",
        "..#...........#..",
        "..#..#..#####.#..",
        "..#...........#..",
        "..#...........#..",
        "..#..#..#####.#..",
        "..#...........#..",
        "..#...........#..",
        "..#..#..###...#..",
        "..#...........#..",
        "..#...........#..",
        "..#############..",
        ".................",
        ".................",
    }},
    {APP_ID_BAZI, {  // four-pillar table
        ".................",
        "..#...#...#...#..",
        "..#...#...#...#..",
        "..#...#...#...#..",
        "..##..##..##..##.",
        "..#...#...#...#..",
        "..#...#...#...#..",
        "..#...#...#...#..",
        "..##..##..##..##.",
        "..#...#...#...#..",
        "..#...#...#...#..",
        "..#...#...#...#..",
        "..##..##..##..##.",
        "..#...#...#...#..",
        "..#...#...#...#..",
        ".................",
        ".................",
    }},
    {APP_ID_CALENDAR, {  // calendar grid
        ".................",
        "..############...",
        "..#...#...#...#..",
        "..#...#...#...#..",
        "..############...",
        "..#...#...#...#..",
        "..#...#...#...#..",
        "..#...#...#...#..",
        "..############...",
        "..#...#...#...#..",
        "..#...#...#...#..",
        "..#...#...#...#..",
        "..#...#...#...#..",
        "..############...",
        ".................",
        ".................",
        ".................",
    }},
    {APP_ID_SETTINGS, {  // sliders
        ".................",
        ".................",
        "....###..........",
        "#####.###########",
        "....###..........",
        ".................",
        ".................",
        "..........###....",
        "###########.#####",
        "..........###....",
        ".................",
        ".................",
        "..###............",
        "###.#############",
        "..###............",
        ".................",
        ".................",
    }},
};

int slot_for_index(size_t index) {
    if (index < 9) return static_cast<int>(index);
    if (index == 9) return 10;  // Settings alone, centered on the fourth row.
    return -1;
}

int row_for_index(size_t index) {
    const int slot = slot_for_index(index);
    return slot < 0 ? 0 : slot / 3;
}

int column_for_index(size_t index) {
    const int slot = slot_for_index(index);
    return slot < 0 ? 0 : slot % 3;
}

uint32_t tile_icon_color(const passport_app_t *app, bool selected) {
    if (!selected) return kPhosDim;
    if (!ui_theme_is_multicolor()) return kPhos;
    switch (app->id) {
        case APP_ID_RADIO: case APP_ID_POMODORO: return UI_TERTIARY;
        case APP_ID_XIAOZHI: case APP_ID_STOPWATCH: case APP_ID_LISTS: return UI_SUCCESS;
        case APP_ID_REMINDERS: case APP_ID_BAZI: return UI_WARNING;
        case APP_ID_NETEASE: return UI_DANGER;
        case APP_ID_CALENDAR: return UI_TERTIARY;
        default: return kPhos;
    }
}

// The icon is drawn into its own tile object; no image or per-part allocation.
void draw_tile_icon_event(lv_event_t *event) {
    const auto *tile = static_cast<const AppTile *>(lv_event_get_user_data(event));
    if (!tile) return;
    const IconGlyph *glyph = nullptr;
    for (const IconGlyph &g : kIconGlyphs)
        if (g.id == tile->id) glyph = &g;
    if (!glyph) return;
    const passport_app_t *app = app_registry_get_by_index(tile->index);
    if (!app) return;
    pixel_scene::Ink ink(event);
    for (int y = 0; y < kGlyphCells; ++y) {
        const char *row = glyph->rows[y];
        for (int x = 0; x < kGlyphCells;) {
            if (row[x] != '#') { ++x; continue; }
            const int start = x;
            while (x < kGlyphCells && row[x] == '#') ++x;
            ink.box(kGlyphOrigin + start * kGlyphCell, kGlyphOrigin + y * kGlyphCell,
                    (x - start) * kGlyphCell, kGlyphCell,
                    tile_icon_color(app, tile->index == s_selected_index));
        }
    }
}

void draw_tile_focus_marker(lv_event_t *event) {
    const auto *tile = static_cast<const AppTile *>(lv_event_get_user_data(event));
    if (!tile || tile->index != s_selected_index) return;
    pixel_scene::Ink ink(event);
    ink.box(3, 3, 4, 4, UI_ACCENT);
}

void apply_selection() {
    if (s_tile_count == 0) return;
    if (s_selected_index >= s_tile_count) s_selected_index = 0;
    for (size_t i = 0; i < s_tile_count; ++i) {
        AppTile &tile = s_tiles[i];
        if (!tile.card) continue;
        const bool selected = i == s_selected_index;
        lv_obj_set_style_bg_opa(tile.card, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(tile.card, lv_color_hex(kPhos), 0);
        lv_obj_set_style_border_opa(tile.card, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(tile.label,
                                    lv_color_hex(selected ? UI_TEXT : UI_TEXT_DIM), 0);
        if (tile.icon) {
            lv_obj_set_y(tile.icon, 4);
            lv_obj_invalidate(tile.icon);
        }
        lv_obj_invalidate(tile.card);
    }
    lv_label_set_text_fmt(s_page_number, "%02u / %02u",
                          (unsigned)s_selected_index + 1, (unsigned)s_tile_count);
}

size_t row_target_index(int direction) {
    const int target_row = (row_for_index(s_selected_index) + direction + 4) % 4;
    const int current_column = column_for_index(s_selected_index);
    size_t best = s_selected_index;
    int best_distance = 100;
    for (size_t i = 0; i < s_tile_count; ++i) {
        if (row_for_index(i) != target_row) continue;
        const int distance = column_for_index(i) > current_column
            ? column_for_index(i) - current_column
            : current_column - column_for_index(i);
        if (distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }
    return best;
}

void icon_y_anim(void *obj, int32_t v) { lv_obj_set_y((lv_obj_t *)obj, v); }

void tile_nudge(size_t index, int direction) {
    if (index >= s_tile_count || !s_tiles[index].icon) return;
    lv_obj_t *icon = s_tiles[index].icon;
    lv_anim_delete(icon, icon_y_anim);
    lv_anim_t b;
    lv_anim_init(&b);
    lv_anim_set_var(&b, icon);
    lv_anim_set_exec_cb(&b, icon_y_anim);
    lv_anim_set_values(&b, 4 + 2 * direction, 4);
    lv_anim_set_duration(&b, 80);
    lv_anim_set_path_cb(&b, lv_anim_path_ease_out);
    lv_anim_start(&b);
}

lv_obj_t *block(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    return obj;
}

}  // namespace

void launcher_ui_init(void) {
    bsp_lvgl_lock(-1);

    s_screen = lv_obj_create(nullptr);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    // 与首页一致的绿磷光底(不再用天空/云/草地的像素风背景)
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(kBg), 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);

    // ---- 状态栏 (y0..24):与首页同款 —— 炭灰条 + 共享 Wi-Fi 格 + 电池 ----
    ui_pixel_top_bar(s_screen);
    ui_pixel_wifi_bars_create(s_screen, UI_STATUS_WIFI_X, 18, s_wifi_bars);
    s_battery = ui_pixel_battery_create(s_screen, UI_STATUS_BATTERY_X, 6, kStatus,
                                        &lv_font_montserrat_14);

    lv_obj_t *catalogue = ui_pixel_label(s_screen, "应用", &buddy_font_16, UI_TEXT);
    ui_pixel_status_text_align(catalogue, 42, 40, LV_TEXT_ALIGN_LEFT);

    lv_obj_t *caption = ui_pixel_label(s_screen, "选择一个应用", &launcher_font_12, UI_TEXT_DIM);
    lv_obj_set_pos(caption, 8, 27);
    lv_obj_set_size(caption, 136, 18);
    s_page_number = ui_pixel_label(s_screen, "", &launcher_font_12, UI_TEXT_DIM);
    lv_obj_set_pos(s_page_number, 160, 27);
    lv_obj_set_size(s_page_number, 72, 18);
    lv_obj_set_style_text_align(s_page_number, LV_TEXT_ALIGN_RIGHT, 0);
    block(s_screen, 8, 40, 224, 1, kPhosBorder);

    const size_t count = app_registry_get_count();
    s_tile_count = count < kMaxTiles ? count : kMaxTiles;
    for (size_t i = 0; i < s_tile_count; ++i) {
        const int slot = slot_for_index(i);
        if (slot < 0) continue;
        const int col = slot % 3;
        const int row = slot / 3;
        const int x = kGridX + col * (kTileW + kColumnGap);
        const int y = kGridY + row * (kTileH + kRowGap);

        AppTile &tile = s_tiles[i];
        tile = {};
        tile.index = i;
        const passport_app_t *app = app_registry_get_by_index(i);
        tile.id = app ? app->id : APP_ID_COUNT;

        tile.card = lv_obj_create(s_screen);
        lv_obj_remove_flag(tile.card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(tile.card, x, y);
        lv_obj_set_size(tile.card, kTileW, kTileH);
        lv_obj_set_style_radius(tile.card, 6, 0);
        lv_obj_set_style_pad_all(tile.card, 0, 0);
        lv_obj_set_style_bg_color(tile.card, lv_color_hex(kCardBg), 0);
        lv_obj_set_style_bg_opa(tile.card, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tile.card, 2, 0);
        lv_obj_set_style_border_color(tile.card, lv_color_hex(kPhos), 0);
        lv_obj_set_style_border_opa(tile.card, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(tile.card, draw_tile_focus_marker, LV_EVENT_DRAW_MAIN, &tile);

        tile.icon = lv_obj_create(tile.card);
        lv_obj_remove_style_all(tile.icon);
        lv_obj_remove_flag(tile.icon, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(tile.icon, (kTileW - kIconSize) / 2, 4);
        lv_obj_set_size(tile.icon, kIconSize, kIconSize);
        lv_obj_add_event_cb(tile.icon, draw_tile_icon_event, LV_EVENT_DRAW_MAIN, &tile);

        tile.label = ui_pixel_label(tile.card, app_chinese_name(tile.id),
                                    &launcher_font_12, UI_TEXT_DIM);
        lv_obj_set_pos(tile.label, 0, 29);
        lv_obj_set_size(tile.label, kTileW, 20);
        lv_obj_set_style_text_align(tile.label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(tile.label, LV_LABEL_LONG_DOT);
    }

    ui_pixel_footer(s_screen, "上下逐项 · 长按跨行",
                    "确定打开 · 长按确定返回", &launcher_font_12);

    if (s_selected_index >= s_tile_count) s_selected_index = 0;
    apply_selection();
    launcher_ui_update_status();
    lv_scr_load(s_screen);
    bsp_lvgl_unlock();
    ESP_LOGI(TAG, "Launcher grid initialized, %u apps", (unsigned)s_tile_count);
}

void launcher_ui_show(void) {
    if (!s_screen) {
        launcher_ui_init();
        return;
    }
    bsp_lvgl_lock(-1);
    apply_selection();
    launcher_ui_update_status();
    lv_scr_load(s_screen);
    bsp_lvgl_unlock();
}

void launcher_ui_hide(void) {
    // Launcher 不与重型 App 同时常驻。Radio 的音频驱动和复杂 UI 会把单核
    // C3 的内部堆推到极限，进入 App 前释放主页可避免 LVGL 分配失败崩溃。
    bsp_lvgl_lock(-1);
    if (s_screen) lv_obj_delete(s_screen);
    s_screen = nullptr;
    s_page_number = nullptr;
    for (AppTile &tile : s_tiles) tile = {};
    s_tile_count = 0;
    s_battery = {nullptr, nullptr};
    for (lv_obj_t *&bar : s_wifi_bars) bar = nullptr;
    bsp_lvgl_unlock();
}

void launcher_ui_update_status(void) {
    if (!s_screen) return;
    ui_pixel_battery_set(&s_battery, bsp_battery_soc());

    const bool connected = WifiManager::GetInstance().IsConnected();
    int bars = 0;
    uint32_t color = kPhosDim;
    if (connected) {
        const int rssi = WifiManager::GetInstance().GetRssi();
        bars = rssi >= -60 ? 3 : (rssi >= -75 ? 2 : 1);
        color = kStatus;
    }
    ui_pixel_wifi_bars_set(s_wifi_bars, bars, color);
}

void launcher_ui_on_key(uint8_t btn, app_btn_event_t event) {
    if (event != BTN_EVT_CLICK && event != BTN_EVT_LONG) return;
    if (!s_tile_count || !s_screen) return;

    if ((btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) &&
        (event == BTN_EVT_CLICK || event == BTN_EVT_LONG)) {
        const int direction = btn == BSP_BTN_UP ? -1 : 1;
        const size_t previous = s_selected_index;
        if (event == BTN_EVT_CLICK) {
            s_selected_index = (s_selected_index + s_tile_count +
                                (direction < 0 ? s_tile_count - 1 : 1)) % s_tile_count;
        } else {
            s_selected_index = row_target_index(direction);
        }
        bsp_lvgl_lock(-1);
        apply_selection();
        tile_nudge(s_selected_index, s_selected_index > previous ? -1 : 1);
        bsp_lvgl_unlock();
    } else if (btn == BSP_BTN_OK && event == BTN_EVT_CLICK) {
        const passport_app_t *app = app_registry_get_by_index(s_selected_index);
        if (app) {
            ESP_LOGI(TAG, "User launched: %s (ID: %d)", app->name, app->id);
            app_registry_switch_to(app->id);
        }
    }
}
