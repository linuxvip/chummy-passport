// Application drawer: shared phosphor palette and pixel icons.
#include "launcher/launcher.h"
#include "launcher/app_registry.h"
#include "launcher/launcher_style.h"
#include "launcher/settings.h"
#include "wifi_manager.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "ui_pixel.h"
#include "ui/pixel_scene.h"
#include "app_fonts.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"

#include <cstdio>
#include <initializer_list>

static const char *TAG = "launcher";

namespace {

constexpr int kViewportY = 25;
constexpr int kViewportH = 295;
constexpr int kCardW = 240;
constexpr int kCardH = 295;
constexpr int kCardStep = 295;
constexpr int kIconSize = 64;
constexpr int kGlyphSpan = 54;
constexpr size_t kMaxTiles = 10;
constexpr size_t kGridColumns = 3;
constexpr size_t kGridRows = 4;
constexpr size_t kGridMaxTiles = 12;
static_assert(kGridColumns * kGridRows == kGridMaxTiles,
              "launcher grid must remain 3 x 4");
constexpr int kGridTileW = 64;
constexpr int kGridTileH = 55;
constexpr int kGridColumnGap = 10;
constexpr int kGridRowGap = 4;
constexpr int kGridLeft = 14;
constexpr int kGridTop = 44;
constexpr int kGridIconSize = 38;
constexpr int kGridLabelY = 36;
constexpr int kGridFooterRuleY = 279;
constexpr int kTileX = (240 - kCardW) / 2;
constexpr int kCardCenterY = (kViewportH - kCardH) / 2;
// Scaling the full 240x295 card asks LVGL for a large temporary buffer. After
// leaving a heavy app the largest internal block can be ~64 KB, so fall back to
// scaling the 64px icon instead of leaving the draw task retrying a failed
// allocation while the registry waits for the LVGL lock.
constexpr size_t kEntryCardAnimMinLargest = 96 * 1024;

// 旅行仪器配色:与首页(app_home.cc)完全同源,风格统一。
#define kBg (UI_BG)          // 屏底炭黑
#define kCardBg (UI_SURFACE)      // 卡片底
#define kPhos (UI_ACCENT)        // 琥珀:图标/标题/选中态
#define kPhosDim (UI_TEXT_DIM)     // 灰米色:副标题/未选中
#define kPhosBorder (UI_BORDER)  // 卡片/底板描边
#define kStatus (ui_theme_is_multicolor() ? UI_TEXT : kPhos)

lv_obj_t *s_screen;
lv_obj_t *s_viewport;
lv_obj_t *s_catalogue;
lv_obj_t *s_accent_flash;
lv_obj_t *s_background_bands[6];
lv_obj_t *s_rail[kMaxTiles];
lv_obj_t *s_up_arrow;
lv_obj_t *s_down_arrow;
lv_obj_t *s_ok_button;
lv_obj_t *s_ok_label;
lv_obj_t *s_wifi_bars[3];
ui_pixel_battery_t s_battery;
size_t s_selected_index;
uint32_t s_active_accent;
bool s_animating;
bool s_opening;
passport_app_id_t s_entry_app = APP_ID_COUNT;
uint8_t s_nav_key = 0xff;
launcher_style_t s_rendered_style = LAUNCHER_STYLE_COUNT;

struct AppTile {
    lv_obj_t *card = nullptr;
    lv_obj_t *icon = nullptr;
    lv_obj_t *label = nullptr;
    lv_obj_t *secondary = nullptr;
    lv_obj_t *ring = nullptr;
    lv_obj_t *accent_bar = nullptr;
    passport_app_id_t id = APP_ID_COUNT;
    size_t index = 0;
};
AppTile s_tiles[kMaxTiles];
size_t s_tile_count;

struct GridTile {
    lv_obj_t *box = nullptr;
    lv_obj_t *icon = nullptr;
    lv_obj_t *label = nullptr;
    lv_obj_t *mark = nullptr;
    passport_app_id_t id = APP_ID_COUNT;
};
GridTile s_grid_tiles[kGridMaxTiles];
lv_obj_t *s_grid_context;
lv_obj_t *s_grid_position;

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

struct IconPalette {
    uint32_t dark_ink;
    uint32_t dark_tile;
    uint32_t light_ink;
    uint32_t light_tile;
};

IconPalette icon_palette(passport_app_id_t id) {
    switch (id) {
        case APP_ID_XIAOZHI: return {0x31D1DF, 0x123B44, 0x14727E, 0xD8EDF0};
        case APP_ID_RADIO: return {0xFFC45A, 0x493514, 0x8D5D13, 0xF2E1BB};
        case APP_ID_NETEASE: return {0xFF7887, 0x48202A, 0xA73C4B, 0xF2D8DC};
        case APP_ID_REMINDERS: return {0xF3D86B, 0x463A16, 0x786317, 0xF1E8C4};
        case APP_ID_LISTS: return {0x73DF97, 0x183C27, 0x3D7A55, 0xDCEDDF};
        case APP_ID_BAZI: return {0xF17D66, 0x45211D, 0xA34B3E, 0xF1DDD7};
        case APP_ID_LIUYAO: return {0x39C7C3, 0x123C3C, 0x14777A, 0xD8EEEB};
        case APP_ID_CALENDAR: return {0x7DBCF6, 0x18364E, 0x3C6690, 0xDDE9F3};
        case APP_ID_REMOTE: return {0x69E5C1, 0x173A35, 0x1E735D, 0xD8F0E8};
        case APP_ID_SETTINGS: return {0xB0C2CE, 0x28343C, 0x58635D, 0xE4E7E3};
        default: return {0xD9CFB9, 0x29343B, 0x5B5145, 0xE4E7E3};
    }
}

bool light_launcher_theme() {
    return ui_theme_get() == UI_THEME_PLAYFUL || ui_theme_get() == UI_THEME_PAPER;
}

uint32_t icon_ink_color(passport_app_id_t id) {
    const IconPalette palette = icon_palette(id);
    return light_launcher_theme() ? palette.light_ink : palette.dark_ink;
}

uint32_t icon_tile_color(passport_app_id_t id) {
    const IconPalette palette = icon_palette(id);
    return light_launcher_theme() ? palette.light_tile : palette.dark_tile;
}

// One line-icon vocabulary: a 17x17 grid of single-pixel cells drawn in the app color,
// strokes one cell wide, no fills or second tone. Each glyph is centered on
// the grid, so new apps add one table entry instead of hand-placed strokes.
constexpr int kGlyphCells = 17;
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
    {APP_ID_LIUYAO, {  // six yao lines with changing marks
        ".................",
        "..#############..",
        ".................",
        "..####...####....",
        ".................",
        "..#############..",
        ".................",
        "..####...####....",
        ".................",
        "..#############..",
        ".................",
        "..####...####....",
        ".................",
        "..#############..",
        ".....O.....X.....",
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
    {APP_ID_REMOTE, {  // phone showing up/down controls
        ".................",
        "....#########....",
        "....#.......#....",
        "....#...#...#....",
        "....#..###..#....",
        "....#.......#....",
        "....#.......#....",
        "....#..###..#....",
        "....#...#...#....",
        "....#.......#....",
        "....#########....",
        "........#........",
        "......#####......",
        ".....#######.....",
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

// The icon is drawn into its own tile object; no image or per-part allocation.
void draw_launcher_icon(lv_event_t *event, lv_obj_t *icon,
                        passport_app_id_t id) {
    if (!icon) return;
    const IconGlyph *glyph = nullptr;
    for (const IconGlyph &g : kIconGlyphs)
        if (g.id == id) glyph = &g;
    if (!glyph) return;
    pixel_scene::Ink ink(event);
    const uint32_t color = icon_ink_color(id);
    const int size = lv_obj_get_width(icon);
    const int span = size * kGlyphSpan / kIconSize;
    const int origin = (size - span) / 2;
    for (int y = 0; y < kGlyphCells; ++y) {
        const char *row = glyph->rows[y];
        for (int x = 0; x < kGlyphCells;) {
            if (row[x] != '#') { ++x; continue; }
            const int start = x;
            while (x < kGlyphCells && row[x] == '#') ++x;
            const int x0 = origin + start * span / kGlyphCells;
            const int x1 = origin + x * span / kGlyphCells;
            const int y0 = origin + y * span / kGlyphCells;
            const int y1 = origin + (y + 1) * span / kGlyphCells;
            ink.box(x0, y0, x1 - x0, y1 - y0, color);
        }
    }
}

void draw_tile_icon_event(lv_event_t *event) {
    const auto *tile = static_cast<const AppTile *>(lv_event_get_user_data(event));
    if (tile) draw_launcher_icon(event, tile->icon, tile->id);
}

void draw_grid_icon_event(lv_event_t *event) {
    const auto *tile = static_cast<const GridTile *>(lv_event_get_user_data(event));
    if (tile) draw_launcher_icon(event, tile->icon, tile->id);
}

int wrap_offset(int offset, int count) {
    if (count <= 1) return 0;
    while (offset > count / 2) offset -= count;
    while (offset < -count / 2) offset += count;
    return offset;
}

int card_y(int offset) {
    return kCardCenterY + wrap_offset(offset, static_cast<int>(s_tile_count)) * kCardStep;
}

uint32_t accent_for(passport_app_id_t id) {
    return icon_ink_color(id);
}

void card_y_anim(void *obj, int32_t value) {
    lv_obj_set_y(static_cast<lv_obj_t *>(obj), value);
}

void opacity_anim(void *obj, int32_t value) {
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), static_cast<lv_opa_t>(value), 0);
}

void icon_scale_anim(void *obj, int32_t value) {
    lv_obj_set_style_transform_scale(static_cast<lv_obj_t *>(obj), value, 0);
}

void key_scale_anim(void *obj, int32_t value) {
    lv_obj_set_style_transform_scale(static_cast<lv_obj_t *>(obj), value, 0);
}

void set_key_feedback(uint8_t button, bool pressed) {
    const uint32_t accent = accent_for(s_tiles[s_selected_index].id);
    if (button == BSP_BTN_UP || button == BSP_BTN_DOWN) {
        lv_obj_t *key = button == BSP_BTN_UP ? s_up_arrow : s_down_arrow;
        if (key == nullptr) return;
        lv_obj_set_style_text_color(
            key, lv_color_hex(pressed ? accent : UI_TEXT_DIM), 0);
        lv_obj_set_style_transform_pivot_x(key, 9, 0);
        lv_obj_set_style_transform_pivot_y(key, 9, 0);
        lv_anim_delete(key, key_scale_anim);
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, key);
        lv_anim_set_exec_cb(&anim, key_scale_anim);
        lv_anim_set_values(&anim, pressed ? 320 : 256, 256);
        lv_anim_set_duration(&anim, 100);
        lv_anim_set_path_cb(&anim, lv_anim_path_overshoot);
        lv_anim_start(&anim);
    } else if (button == BSP_BTN_OK) {
        if (s_ok_button == nullptr) return;
        lv_obj_set_style_bg_opa(s_ok_button, pressed ? 90 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_ok_button, pressed ? 2 : 1, 0);
        lv_obj_set_style_border_color(
            s_ok_button, lv_color_hex(pressed ? accent : UI_BORDER), 0);
        if (s_ok_label != nullptr) {
            lv_obj_set_style_text_color(
                s_ok_label, lv_color_hex(pressed ? accent : UI_TEXT_DIM), 0);
        }
    }
}

void update_accent(uint32_t accent) {
    s_active_accent = accent;
    if (s_accent_flash != nullptr) {
        lv_anim_delete(s_accent_flash, opacity_anim);
        lv_obj_set_style_bg_opa(s_accent_flash, 54, 0);
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, s_accent_flash);
        lv_anim_set_exec_cb(&anim, opacity_anim);
        lv_anim_set_values(&anim, 54, LV_OPA_TRANSP);
        lv_anim_set_duration(&anim, 260);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
        lv_anim_start(&anim);
    }
    if (s_up_arrow != nullptr) {
        if (s_nav_key != BSP_BTN_UP) {
            lv_obj_set_style_text_color(s_up_arrow, lv_color_hex(UI_TEXT_DIM), 0);
        }
    }
    if (s_down_arrow != nullptr) {
        if (s_nav_key != BSP_BTN_DOWN) {
            lv_obj_set_style_text_color(s_down_arrow, lv_color_hex(UI_TEXT_DIM), 0);
        }
    }
    if (s_ok_button != nullptr && s_nav_key != BSP_BTN_OK) {
        lv_obj_set_style_border_color(s_ok_button, lv_color_hex(accent), 0);
    }
}

void apply_tile_style(size_t selected_index) {
    for (size_t i = 0; i < s_tile_count; ++i) {
        AppTile &tile = s_tiles[i];
        if (tile.card == nullptr) continue;
        const bool selected = i == selected_index;
        lv_obj_set_style_bg_opa(tile.card, LV_OPA_TRANSP, 0);
        if (tile.ring != nullptr) {
            lv_obj_set_style_border_color(tile.ring,
                                          lv_color_hex(accent_for(tile.id)), 0);
        }
        lv_obj_set_style_text_color(tile.label,
                                    lv_color_hex(selected ? UI_TEXT : UI_TEXT_DIM), 0);
        if (tile.secondary != nullptr) {
            lv_obj_set_style_text_color(
                tile.secondary,
                lv_color_hex(accent_for(tile.id)), 0);
        }
        if (tile.accent_bar != nullptr) {
            lv_obj_set_style_bg_color(tile.accent_bar,
                                      lv_color_hex(accent_for(tile.id)), 0);
            lv_obj_set_style_bg_opa(tile.accent_bar, LV_OPA_COVER, 0);
        }
        if (tile.icon != nullptr) {
            lv_obj_set_style_bg_color(tile.icon, lv_color_hex(icon_tile_color(tile.id)), 0);
            lv_obj_set_style_border_color(tile.icon, lv_color_hex(icon_ink_color(tile.id)), 0);
            lv_obj_set_style_border_width(tile.icon, 0, 0);
            lv_obj_set_style_bg_opa(tile.icon, LV_OPA_TRANSP, 0);
            lv_obj_invalidate(tile.icon);
        }
        lv_obj_invalidate(tile.card);
    }

    for (size_t i = 0; i < s_tile_count; ++i) {
        if (s_rail[i] == nullptr) continue;
        const bool selected = i == selected_index;
        lv_obj_set_style_bg_color(s_rail[i],
                                  lv_color_hex(accent_for(s_tiles[i].id)), 0);
        lv_obj_set_style_bg_opa(s_rail[i], selected ? LV_OPA_COVER : 170, 0);
        lv_obj_set_size(s_rail[i], selected ? 9 : 6, selected ? 24 : 6);
        lv_obj_set_x(s_rail[i], selected ? 26 : 27);
        lv_obj_set_y(s_rail[i],
                     94 + static_cast<int>(i) * 19 - (selected ? 9 : 0));
    }

    const uint32_t accent = accent_for(s_tiles[selected_index].id);
    update_accent(accent);
}

void selection_finished(lv_anim_t *anim) {
    s_animating = false;
    s_nav_key = 0xff;
    set_key_feedback(BSP_BTN_UP, false);
    set_key_feedback(BSP_BTN_DOWN, false);
    (void)anim;
}

void move_selection(int delta) {
    if (s_tile_count == 0 || s_animating || s_opening) return;
    const int count = static_cast<int>(s_tile_count);
    const int old_index = static_cast<int>(s_selected_index);
    const int new_index = (old_index + delta + count) % count;
    s_animating = true;
    s_nav_key = delta > 0 ? BSP_BTN_DOWN : BSP_BTN_UP;
    set_key_feedback(s_nav_key, true);

    for (size_t i = 0; i < s_tile_count; ++i) {
        AppTile &tile = s_tiles[i];
        if (tile.card == nullptr) continue;
        const int old_offset = wrap_offset(static_cast<int>(i) - old_index, count);
        const int target_offset = wrap_offset(old_offset - delta, count);
        if (tile.icon != nullptr) {
            lv_anim_delete(tile.icon, icon_scale_anim);
            const int32_t target_scale =
                static_cast<int>(i) == new_index ? 256 : 200;
            const int32_t source_scale =
                static_cast<int>(i) == new_index ? 196 : 256;
            lv_anim_t scale;
            lv_anim_init(&scale);
            lv_anim_set_var(&scale, tile.icon);
            lv_anim_set_exec_cb(&scale, icon_scale_anim);
            lv_anim_set_values(&scale, source_scale, target_scale);
            lv_anim_set_duration(&scale, delta == 1 || delta == -1 ? 220 : 320);
            lv_anim_set_path_cb(&scale, lv_anim_path_overshoot);
            lv_anim_start(&scale);
        }
        const int target_y = card_y(target_offset);
        lv_anim_delete(tile.card, card_y_anim);
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, tile.card);
        lv_anim_set_exec_cb(&anim, card_y_anim);
        lv_anim_set_values(&anim, lv_obj_get_y(tile.card), target_y);
        lv_anim_set_duration(&anim, delta == 1 || delta == -1 ? 220 : 320);
        lv_anim_set_path_cb(&anim, lv_anim_path_overshoot);
        if (static_cast<int>(i) == new_index) {
            lv_anim_set_completed_cb(&anim, selection_finished);
        }
        lv_anim_start(&anim);
    }

    s_selected_index = static_cast<size_t>(new_index);
    apply_tile_style(s_selected_index);
    lv_obj_move_foreground(s_tiles[s_selected_index].card);
}

void entry_scale_anim(void *obj, int32_t value) {
    lv_obj_set_style_transform_scale(static_cast<lv_obj_t *>(obj), value, 0);
}

void commit_entry(lv_timer_t *timer) {
    lv_timer_delete(timer);
    if (s_entry_app != APP_ID_COUNT) app_registry_switch_to(s_entry_app);
}

void start_entry() {
    if (s_tile_count == 0 || s_animating || s_opening) return;
    const passport_app_t *app = app_registry_get_by_index(s_selected_index);
    if (app == nullptr) return;
    s_opening = true;
    s_nav_key = BSP_BTN_OK;
    set_key_feedback(BSP_BTN_OK, true);
    s_entry_app = app->id;
    AppTile &tile = s_tiles[s_selected_index];
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    lv_anim_delete(tile.card, entry_scale_anim);
    if (largest >= kEntryCardAnimMinLargest) {
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, tile.card);
        lv_anim_set_exec_cb(&anim, entry_scale_anim);
        lv_anim_set_values(&anim, 256, 96);
        lv_anim_set_duration(&anim, 220);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
        lv_anim_start(&anim);
        ESP_LOGI(TAG, "Entry: card scale, largest=%u", (unsigned)largest);
    } else if (tile.icon != nullptr) {
        lv_obj_set_style_transform_scale(tile.card, 256, 0);
        lv_anim_delete(tile.icon, icon_scale_anim);
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, tile.icon);
        lv_anim_set_exec_cb(&anim, icon_scale_anim);
        lv_anim_set_values(&anim, 256, 128);
        lv_anim_set_duration(&anim, 180);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
        lv_anim_start(&anim);
        ESP_LOGW(TAG, "Entry: low heap, icon scale only, largest=%u",
                 (unsigned)largest);
    } else {
        ESP_LOGW(TAG, "Entry: low heap, no scale, largest=%u",
                 (unsigned)largest);
    }
    lv_timer_create(commit_entry, 230, nullptr);
}

void layout_tiles_immediate() {
    for (size_t i = 0; i < s_tile_count; ++i) {
        const int offset = wrap_offset(static_cast<int>(i) -
                                       static_cast<int>(s_selected_index),
                                       static_cast<int>(s_tile_count));
        lv_obj_set_pos(s_tiles[i].card, kTileX, card_y(offset));
    }
    lv_obj_move_foreground(s_tiles[s_selected_index].card);
}

void apply_selection() {
    if (s_tile_count == 0) return;
    if (s_selected_index >= s_tile_count) s_selected_index = 0;
    apply_tile_style(s_selected_index);
    layout_tiles_immediate();
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

void apply_grid_tile_style(size_t selected_index) {
    for (size_t i = 0; i < kGridMaxTiles; ++i) {
        GridTile &tile = s_grid_tiles[i];
        if (!tile.box) continue;
        const bool selected = i == selected_index;
        const uint32_t accent = accent_for(tile.id);
        ui_pixel_bg_color(tile.box, UI_SURFACE_ALT);
        lv_obj_set_style_bg_opa(tile.box, selected ? LV_OPA_70 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tile.box, selected ? 1 : 0, 0);
        lv_obj_set_style_border_color(tile.box, lv_color_hex(accent), 0);
        lv_obj_set_flag(tile.mark, LV_OBJ_FLAG_HIDDEN, !selected);

        lv_obj_set_style_bg_opa(tile.icon, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tile.icon, 0, 0);
        lv_obj_set_style_opa(tile.icon, selected ? LV_OPA_COVER : LV_OPA_70, 0);
        ui_pixel_text_color(tile.label, selected ? UI_TEXT : UI_TEXT_DIM);
        lv_obj_invalidate(tile.box);
        lv_obj_invalidate(tile.icon);
    }
    if (s_grid_position && s_tile_count) {
        char position[16];
        std::snprintf(position, sizeof(position), "%02u / %02u",
                      static_cast<unsigned>(selected_index + 1),
                      static_cast<unsigned>(s_tile_count));
        ui_pixel_label_set_text(s_grid_position, position);
    }
}

void grid_focus_pulse(GridTile &tile) {
    if (!tile.icon) return;
    lv_anim_delete(tile.icon, icon_scale_anim);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, tile.icon);
    lv_anim_set_exec_cb(&anim, icon_scale_anim);
    lv_anim_set_values(&anim, 288, 256);
    lv_anim_set_duration(&anim, 100);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}

void grid_move_selection(int delta) {
    if (!s_tile_count || s_opening) return;
    const int count = static_cast<int>(s_tile_count);
    const int next = (static_cast<int>(s_selected_index) + delta + count) % count;
    s_selected_index = static_cast<size_t>(next);
    apply_grid_tile_style(s_selected_index);
    grid_focus_pulse(s_grid_tiles[s_selected_index]);
}

void grid_start_entry() {
    if (!s_tile_count || s_opening) return;
    const passport_app_t *app = app_registry_get_by_index(s_selected_index);
    if (!app) return;
    s_opening = true;
    s_nav_key = BSP_BTN_OK;
    s_entry_app = app->id;
    ESP_LOGI(TAG, "User launched grid app: %s (ID: %d)", app->name, app->id);

    GridTile &tile = s_grid_tiles[s_selected_index];
    if (tile.icon) {
        lv_anim_delete(tile.icon, icon_scale_anim);
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, tile.icon);
        lv_anim_set_exec_cb(&anim, icon_scale_anim);
        lv_anim_set_values(&anim, 256, 128);
        lv_anim_set_duration(&anim, 140);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
        lv_anim_start(&anim);
    }
    lv_timer_create(commit_entry, 150, nullptr);
}

void build_grid_ui_locked() {
    s_screen = lv_obj_create(nullptr);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(kBg), 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);

    for (size_t i = 0; i < sizeof(s_background_bands) / sizeof(s_background_bands[0]); ++i) {
        lv_obj_t *band = block(s_screen, 0, 25 + static_cast<int>(i) * 49,
                               240, 49, kCardBg);
        lv_obj_set_style_bg_opa(band, static_cast<lv_opa_t>(9 + i * 3), 0);
        s_background_bands[i] = band;
    }

    ui_pixel_top_bar(s_screen);
    ui_pixel_wifi_bars_create(s_screen, UI_STATUS_WIFI_X, 18, s_wifi_bars);
    s_battery = ui_pixel_battery_create(s_screen, UI_STATUS_BATTERY_X, 6, kStatus,
                                        &lv_font_montserrat_14);
    s_catalogue = ui_pixel_label(s_screen, "选择应用", &buddy_font_16, UI_TEXT);
    ui_pixel_status_text_align(s_catalogue, 42, 112, LV_TEXT_ALIGN_LEFT);

    s_grid_context = ui_pixel_label(s_screen, "GRID 3x4", &launcher_font_12,
                                    UI_TEXT_DIM);
    lv_obj_set_pos(s_grid_context, 12, 31);
    lv_obj_set_size(s_grid_context, 100, 14);
    s_grid_position = ui_pixel_label(s_screen, "", &launcher_font_12,
                                     UI_TEXT);
    lv_obj_set_pos(s_grid_position, 168, 31);
    lv_obj_set_size(s_grid_position, 60, 14);
    lv_obj_set_style_text_align(s_grid_position, LV_TEXT_ALIGN_RIGHT, 0);

    const size_t count = app_registry_get_count();
    s_tile_count = count < kGridMaxTiles ? count : kGridMaxTiles;
    if (s_selected_index >= s_tile_count) s_selected_index = 0;
    for (size_t i = 0; i < s_tile_count; ++i) {
        GridTile &tile = s_grid_tiles[i];
        tile = {};
        const passport_app_t *app = app_registry_get_by_index(i);
        tile.id = app ? app->id : APP_ID_COUNT;
        const size_t row = i / kGridColumns;
        const size_t col = i % kGridColumns;
        const int x = kGridLeft + static_cast<int>(col) * (kGridTileW + kGridColumnGap);
        const int y = kGridTop + static_cast<int>(row) * (kGridTileH + kGridRowGap);

        tile.box = lv_obj_create(s_screen);
        lv_obj_remove_flag(tile.box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(tile.box, x, y);
        lv_obj_set_size(tile.box, kGridTileW, kGridTileH);
        lv_obj_set_style_radius(tile.box, 0, 0);
        lv_obj_set_style_pad_all(tile.box, 0, 0);
        lv_obj_set_style_bg_opa(tile.box, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tile.box, 0, 0);

        tile.mark = block(tile.box, 2, 0, 17, 2, accent_for(tile.id));
        lv_obj_add_flag(tile.mark, LV_OBJ_FLAG_HIDDEN);

        tile.icon = lv_obj_create(tile.box);
        lv_obj_remove_style_all(tile.icon);
        lv_obj_remove_flag(tile.icon, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(tile.icon, (kGridTileW - kGridIconSize) / 2, 0);
        lv_obj_set_size(tile.icon, kGridIconSize, kGridIconSize);
        lv_obj_set_style_radius(tile.icon, 0, 0);
        lv_obj_set_style_bg_opa(tile.icon, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tile.icon, 0, 0);
        lv_obj_set_style_transform_pivot_x(tile.icon, kGridIconSize / 2, 0);
        lv_obj_set_style_transform_pivot_y(tile.icon, kGridIconSize / 2, 0);
        lv_obj_add_event_cb(tile.icon, draw_grid_icon_event, LV_EVENT_DRAW_MAIN, &tile);

        tile.label = ui_pixel_label(tile.box, app_chinese_name(tile.id),
                                    &launcher_font_12, UI_TEXT_DIM);
        lv_obj_set_pos(tile.label, 0, kGridLabelY);
        lv_obj_set_size(tile.label, kGridTileW, 16);
        lv_obj_set_style_text_align(tile.label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(tile.label, LV_LABEL_LONG_DOT);
    }

    block(s_screen, 12, kGridFooterRuleY, 216, 1, UI_BORDER);
    lv_obj_t *primary = ui_pixel_label(s_screen, "上下逐项 · 长按跨行",
                                       &launcher_font_12, UI_TEXT_DIM);
    lv_obj_set_pos(primary, 12, 281);
    lv_obj_set_size(primary, 216, 16);
    lv_obj_set_style_text_align(primary, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *secondary = ui_pixel_label(s_screen, "确定打开 · 长按确定返回",
                                         &launcher_font_12, UI_TEXT_DIM);
    lv_obj_set_pos(secondary, 12, 297);
    lv_obj_set_size(secondary, 216, 16);
    lv_obj_set_style_text_align(secondary, LV_TEXT_ALIGN_CENTER, 0);

    apply_grid_tile_style(s_selected_index);
    s_rendered_style = LAUNCHER_STYLE_GRID;
    launcher_ui_update_status();
    lv_scr_load(s_screen);
    ESP_LOGI(TAG, "Launcher grid initialized, %u apps", (unsigned)s_tile_count);
}

}  // namespace

void launcher_ui_init(void) {
    if (s_screen) launcher_ui_hide();
    bsp_lvgl_lock(-1);

    if (settings_get_launcher_style() == LAUNCHER_STYLE_GRID) {
        build_grid_ui_locked();
        bsp_lvgl_unlock();
        return;
    }

    s_screen = lv_obj_create(nullptr);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(kBg), 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);

    // A fixed low-contrast band texture keeps the page background stable across
    // apps; only foreground identity colours change with the selection.
    for (size_t i = 0; i < sizeof(s_background_bands) / sizeof(s_background_bands[0]); ++i) {
        lv_obj_t *band = block(s_screen, 0, 25 + static_cast<int>(i) * 49,
                               240, 49, kCardBg);
        lv_obj_set_style_bg_opa(band, static_cast<lv_opa_t>(9 + i * 3), 0);
        s_background_bands[i] = band;
    }
    // One full-height page per app moves through this clipped viewport.
    s_viewport = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_viewport);
    lv_obj_remove_flag(s_viewport, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_viewport, 0, kViewportY);
    lv_obj_set_size(s_viewport, 240, kViewportH);
    lv_obj_set_style_bg_opa(s_viewport, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_viewport, 0, 0);
    // Neutral flash above the pages gives a visible transition without
    // changing the page's resting background colour.
    s_accent_flash = block(s_screen, 0, 25, 240, 295, UI_TEXT);
    lv_obj_set_style_bg_opa(s_accent_flash, LV_OPA_TRANSP, 0);

    ui_pixel_top_bar(s_screen);
    ui_pixel_wifi_bars_create(s_screen, UI_STATUS_WIFI_X, 18, s_wifi_bars);
    s_battery = ui_pixel_battery_create(s_screen, UI_STATUS_BATTERY_X, 6, kStatus,
                                        &lv_font_montserrat_14);

    s_catalogue = ui_pixel_label(s_screen, "选择应用", &buddy_font_16, UI_TEXT);
    ui_pixel_status_text_align(s_catalogue, 42, 112, LV_TEXT_ALIGN_LEFT);

    const size_t count = app_registry_get_count();
    s_tile_count = count < kMaxTiles ? count : kMaxTiles;
    for (size_t i = 0; i < s_tile_count; ++i) {
        AppTile &tile = s_tiles[i];
        tile = {};
        tile.index = i;
        const passport_app_t *app = app_registry_get_by_index(i);
        tile.id = app ? app->id : APP_ID_COUNT;

        tile.card = lv_obj_create(s_viewport);
        lv_obj_remove_flag(tile.card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(tile.card, kTileX, 0);
        lv_obj_set_size(tile.card, kCardW, kCardH);
        lv_obj_set_style_pad_all(tile.card, 0, 0);
        lv_obj_set_style_bg_opa(tile.card, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tile.card, 0, 0);
        lv_obj_set_style_transform_pivot_x(tile.card, kCardW / 2, 0);
        lv_obj_set_style_transform_pivot_y(tile.card, kCardH / 2, 0);

        tile.ring = lv_obj_create(tile.card);
        lv_obj_remove_style_all(tile.ring);
        lv_obj_remove_flag(tile.ring, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(tile.ring, 57, 35);
        lv_obj_set_size(tile.ring, 126, 126);
        lv_obj_set_style_radius(tile.ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(tile.ring, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tile.ring, 2, 0);
        lv_obj_set_style_border_color(tile.ring,
                                      lv_color_hex(accent_for(tile.id)), 0);
        lv_obj_set_style_border_opa(tile.ring, 220, 0);

        tile.accent_bar = lv_obj_create(tile.card);
        lv_obj_remove_style_all(tile.accent_bar);
        lv_obj_remove_flag(tile.accent_bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(tile.accent_bar, 69, 188);
        lv_obj_set_size(tile.accent_bar, 3, 42);
        lv_obj_set_style_radius(tile.accent_bar, 2, 0);
        lv_obj_set_style_bg_opa(tile.accent_bar, LV_OPA_COVER, 0);

        tile.icon = lv_obj_create(tile.card);
        lv_obj_remove_style_all(tile.icon);
        lv_obj_remove_flag(tile.icon, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(tile.icon, 88, 66);
        lv_obj_set_size(tile.icon, kIconSize, kIconSize);
        lv_obj_set_style_bg_opa(tile.icon, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tile.icon, 0, 0);
        lv_obj_add_event_cb(tile.icon, draw_tile_icon_event, LV_EVENT_DRAW_MAIN, &tile);

        tile.secondary = ui_pixel_label(
            tile.card, "", &launcher_font_12, accent_for(tile.id));
        lv_label_set_text_fmt(
            tile.secondary, "%02u  %s", static_cast<unsigned>(i + 1),
            app != nullptr && app->en_name != nullptr ? app->en_name : "");
        lv_obj_set_pos(tile.secondary, 77, 190);
        lv_obj_set_size(tile.secondary, 140, 18);
        lv_obj_set_style_text_align(tile.secondary, LV_TEXT_ALIGN_LEFT, 0);

        tile.label = ui_pixel_label(tile.card, app_chinese_name(tile.id),
                                    &launcher_title_20, UI_TEXT);
        lv_obj_set_pos(tile.label, 77, 207);
        lv_obj_set_size(tile.label, 140, 26);
        lv_obj_set_style_text_align(tile.label, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_long_mode(tile.label, LV_LABEL_LONG_DOT);

        lv_obj_set_style_transform_pivot_x(tile.icon, kIconSize / 2, 0);
        lv_obj_set_style_transform_pivot_y(tile.icon, kIconSize / 2, 0);
    }

    for (size_t i = 0; i < s_tile_count; ++i) {
        s_rail[i] = lv_obj_create(s_screen);
        lv_obj_remove_style_all(s_rail[i]);
        lv_obj_remove_flag(s_rail[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(s_rail[i], 27, 94 + static_cast<int>(i) * 19);
        lv_obj_set_size(s_rail[i], 6, 6);
        lv_obj_set_style_radius(s_rail[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_rail[i],
                                  lv_color_hex(accent_for(s_tiles[i].id)), 0);
        lv_obj_set_style_bg_opa(s_rail[i], 170, 0);
    }

    s_up_arrow = ui_pixel_label(s_screen, "▲", &buddy_font_16, UI_TEXT_DIM);
    lv_obj_set_pos(s_up_arrow, 210, 46);
    lv_obj_set_size(s_up_arrow, 18, 18);
    lv_obj_set_style_text_align(s_up_arrow, LV_TEXT_ALIGN_CENTER, 0);
    s_down_arrow = ui_pixel_label(s_screen, "▼", &buddy_font_16, UI_TEXT_DIM);
    lv_obj_set_pos(s_down_arrow, 210, 150);
    lv_obj_set_size(s_down_arrow, 18, 18);
    lv_obj_set_style_text_align(s_down_arrow, LV_TEXT_ALIGN_CENTER, 0);
    s_ok_button = lv_obj_create(s_screen);
    lv_obj_remove_flag(s_ok_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_ok_button, 201, 269);
    lv_obj_set_size(s_ok_button, 36, 36);
    lv_obj_set_style_radius(s_ok_button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_ok_button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ok_button, 1, 0);
    lv_obj_set_style_border_color(s_ok_button, lv_color_hex(UI_BORDER), 0);
    lv_obj_set_style_border_opa(s_ok_button, 190, 0);
    lv_obj_set_style_pad_all(s_ok_button, 0, 0);
    s_ok_label = ui_pixel_label(s_ok_button, "OK", &lv_font_montserrat_14,
                                UI_TEXT_DIM);
    lv_obj_center(s_ok_label);

    if (s_selected_index >= s_tile_count) s_selected_index = 0;
    apply_selection();
    s_rendered_style = LAUNCHER_STYLE_FLOW;
    launcher_ui_update_status();
    lv_scr_load(s_screen);
    bsp_lvgl_unlock();
    ESP_LOGI(TAG, "Passport flow initialized, %u apps", (unsigned)s_tile_count);
}

void launcher_ui_show(void) {
    const launcher_style_t style = settings_get_launcher_style();
    if (!s_screen || s_rendered_style != style) {
        launcher_ui_init();
        return;
    }
    bsp_lvgl_lock(-1);
    if (s_rendered_style == LAUNCHER_STYLE_GRID) {
        apply_grid_tile_style(s_selected_index);
    } else {
        apply_selection();
    }
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
    s_viewport = nullptr;
    s_catalogue = nullptr;
    s_accent_flash = nullptr;
    s_up_arrow = nullptr;
    s_down_arrow = nullptr;
    s_ok_button = nullptr;
    s_ok_label = nullptr;
    s_grid_context = nullptr;
    s_grid_position = nullptr;
    s_active_accent = 0;
    s_animating = false;
    s_opening = false;
    s_entry_app = APP_ID_COUNT;
    s_rendered_style = LAUNCHER_STYLE_COUNT;
    for (AppTile &tile : s_tiles) tile = {};
    for (GridTile &tile : s_grid_tiles) tile = {};
    for (lv_obj_t *&band : s_background_bands) band = nullptr;
    for (lv_obj_t *&dot : s_rail) dot = nullptr;
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
    if (!s_tile_count || !s_screen) return;

    bsp_lvgl_lock(-1);
    if (s_rendered_style == LAUNCHER_STYLE_GRID) {
        if (event == BTN_EVT_CLICK || event == BTN_EVT_LONG) {
            if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
                const int direction = btn == BSP_BTN_UP ? -1 : 1;
                grid_move_selection(direction *
                                    (event == BTN_EVT_LONG ? static_cast<int>(kGridColumns) : 1));
            } else if (btn == BSP_BTN_OK && event == BTN_EVT_CLICK) {
                grid_start_entry();
            }
        }
        bsp_lvgl_unlock();
        return;
    }
    if (event == BTN_EVT_PRESS) {
        set_key_feedback(btn, true);
        bsp_lvgl_unlock();
        return;
    }
    if (event == BTN_EVT_RELEASE || event == BTN_EVT_LONG_UP) {
        if (!s_animating && !s_opening) set_key_feedback(btn, false);
        bsp_lvgl_unlock();
        return;
    }
    if (event != BTN_EVT_CLICK && event != BTN_EVT_LONG) {
        bsp_lvgl_unlock();
        return;
    }
    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        const int direction = btn == BSP_BTN_UP ? -1 : 1;
        move_selection(direction * (event == BTN_EVT_LONG ? 4 : 1));
    } else if (btn == BSP_BTN_OK && event == BTN_EVT_CLICK) {
        const passport_app_t *app = app_registry_get_by_index(s_selected_index);
        if (app != nullptr) {
            ESP_LOGI(TAG, "User launched: %s (ID: %d)", app->name, app->id);
        }
        start_entry();
    }
    bsp_lvgl_unlock();
}
