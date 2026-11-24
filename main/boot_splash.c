// main/boot_splash.c — "命海日出" boot animation.
// A gold sun rises from geometric waves while fourteen cells fill in step with
// the boot chime. Every element is drawn with LVGL primitives; no bitmap asset
// or extra decoder memory is required.
#include "boot_splash.h"
#include "app_fonts.h"

#include "bsp_display.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdbool.h>

static const char *TAG = "boot_splash";

static const uint32_t kSkyTop = 0x020B12;
static const uint32_t kSkyBottom = 0x092730;
static const uint32_t kSurface = 0x0B2C35;
static const uint32_t kGold = 0xF0C76B;
static const uint32_t kCream = 0xEEE7D6;
static const uint32_t kDim = 0x8BA6A5;
static const uint32_t kCyan = 0x7DD6D1;

#define kScreenW 240
#define kScreenH 320
#define kBarN 14
#define kStageN 4
#define kStageMs 80

// Line widgets keep the pointer, so every point array must outlive the screen.
static lv_point_precise_t s_ray_points[11][2];

static const lv_point_precise_t s_horizon_points[] = {
    {29, 171}, {211, 171},
};
static const lv_point_precise_t s_wave_1[] = {
    {23, 188}, {52, 178}, {73, 190}, {101, 180}, {120, 186},
    {139, 180}, {167, 190}, {188, 178}, {217, 188},
};
static const lv_point_precise_t s_wave_2[] = {
    {17, 203}, {49, 192}, {81, 204}, {120, 194}, {159, 204},
    {191, 192}, {223, 203},
};
static const lv_point_precise_t s_wave_3[] = {
    {28, 218}, {67, 207}, {101, 219}, {120, 213}, {139, 219},
    {173, 207}, {212, 218},
};

static lv_obj_t *s_splash;
static lv_obj_t *s_brand;
static lv_obj_t *s_sun_stage;
static lv_obj_t *s_horizon;
static lv_obj_t *s_title;
static lv_obj_t *s_subtitle;
static lv_obj_t *s_footer;
static lv_obj_t *s_stars[7];
static lv_obj_t *s_rays[11];
static lv_obj_t *s_waves[3];
static lv_obj_t *s_cells[kBarN];
static int s_step;
static volatile bool s_animation_done;

static lv_obj_t *block(lv_obj_t *parent, int x, int y, int w, int h,
                       uint32_t color, lv_opa_t opacity) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, opacity, 0);
    return obj;
}

static lv_obj_t *dot(lv_obj_t *parent, int x, int y, int diameter,
                     uint32_t color, lv_opa_t opacity) {
    lv_obj_t *obj = block(parent, x, y, diameter, diameter, color, opacity);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    return obj;
}

static lv_obj_t *line(lv_obj_t *parent, const lv_point_precise_t *points,
                      uint32_t point_count, uint32_t color, int width,
                      lv_opa_t opacity) {
    lv_obj_t *obj = lv_line_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, kScreenW, kScreenH);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_line_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_line_width(obj, width, 0);
    lv_obj_set_style_line_opa(obj, opacity, 0);
    lv_obj_set_style_line_rounded(obj, true, 0);
    lv_line_set_points(obj, points, point_count);
    return obj;
}

static lv_obj_t *text_line(lv_obj_t *parent, const char *value,
                           const lv_font_t *font, uint32_t color, int y,
                           int height, int letter_space) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, 0, y);
    lv_obj_set_size(label, kScreenW, height);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(label, letter_space, 0);
    lv_label_set_text(label, value);
    return label;
}

static void set_opacity(lv_obj_t *obj, uint8_t value) {
    lv_obj_set_style_opa(obj, (lv_opa_t)value, 0);
}

static uint8_t clamp_opacity(int value) {
    return (uint8_t)(value > 255 ? 255 : value);
}

static void set_line_opacity(lv_obj_t *obj, uint8_t value) {
    lv_obj_set_style_line_opa(obj, (lv_opa_t)value, 0);
}

static void dawn_step(lv_timer_t *timer) {
    (void)timer;
    const int stage = s_step++;
    const int filled = (kBarN * (stage + 1)) / kStageN;

    for (int i = 0; i < filled; ++i) {
        lv_obj_set_style_bg_color(s_cells[i], lv_color_hex(kGold), 0);
        set_opacity(s_cells[i], LV_OPA_COVER);
    }
    lv_obj_set_y(s_sun_stage, 100 - (29 * filled) / kBarN);
    set_opacity(s_brand, clamp_opacity((stage + 1) * 64));

    const int title_opa = (stage + 1) * 68;
    set_opacity(s_title, clamp_opacity(title_opa));
    lv_obj_set_y(s_title, 229 - (stage < 3 ? stage : 3));
    if (stage >= 1) set_opacity(s_subtitle, clamp_opacity(stage * 90));
    if (stage >= 2) set_opacity(s_footer, clamp_opacity((stage - 1) * 120));

    static const uint8_t ray_levels[] = {24, 56, 82, 96};
    const uint8_t ray_opa = ray_levels[stage];
    for (int i = 0; i < 11; ++i) set_line_opacity(s_rays[i], ray_opa);
    set_line_opacity(s_waves[0], (uint8_t)(62 + stage * 7));
    if (stage >= 1) set_line_opacity(s_waves[1], 76);
    if (stage >= 2) set_line_opacity(s_waves[2], 48);
    set_line_opacity(s_horizon, (uint8_t)(80 + stage * 46));
    for (int i = 0; i < 7; ++i) {
        set_opacity(s_stars[i], (uint8_t)(stage == 0 ? 70 : 140));
    }
    if (stage + 1 == kStageN) s_animation_done = true;
}

void boot_splash_run(void) {
    bsp_lvgl_lock(-1);

    // Wait for any in-flight panel transaction before replacing the screen.
    esp_lcd_panel_io_handle_t io = bsp_display_io();
    if (io) esp_lcd_panel_io_tx_param(io, 0x00, NULL, 0);

    s_splash = lv_obj_create(NULL);
    lv_obj_remove_flag(s_splash, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_splash, lv_color_hex(kSkyTop), 0);
    lv_obj_set_style_bg_grad_color(s_splash, lv_color_hex(kSkyBottom), 0);
    lv_obj_set_style_bg_grad_dir(s_splash, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_splash, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_splash, 0, 0);
    lv_obj_set_style_border_width(s_splash, 0, 0);
    lv_obj_set_style_outline_width(s_splash, 0, 0);
    lv_obj_set_style_shadow_width(s_splash, 0, 0);
    lv_obj_set_style_pad_all(s_splash, 0, 0);

    s_brand = text_line(s_splash, "PASSPORT OS",
                        &lv_font_montserrat_14, kDim, 15, 20, 3);

    // Sparse stars remain readable without a second animation timer.
    s_stars[0] = dot(s_splash, 28, 57, 2, kCream, 150);
    s_stars[1] = dot(s_splash, 60, 38, 1, kCream, 100);
    s_stars[2] = dot(s_splash, 88, 70, 1, kCream, 125);
    s_stars[3] = dot(s_splash, 153, 42, 2, kCream, 120);
    s_stars[4] = dot(s_splash, 185, 68, 1, kCream, 110);
    s_stars[5] = dot(s_splash, 212, 36, 2, kCream, 135);
    s_stars[6] = dot(s_splash, 205, 108, 1, kCream, 85);

    // The stage is larger than the sun so rays can rise with it.
    s_sun_stage = lv_obj_create(s_splash);
    lv_obj_remove_flag(s_sun_stage, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_sun_stage, 40, 100);
    lv_obj_set_size(s_sun_stage, 160, 160);
    lv_obj_set_style_pad_all(s_sun_stage, 0, 0);
    lv_obj_set_style_border_width(s_sun_stage, 0, 0);
    lv_obj_set_style_bg_opa(s_sun_stage, LV_OPA_TRANSP, 0);

    static const int ray[11][4] = {
        {33, 80, 20, 80}, {35, 69, 23, 65}, {42, 52, 31, 45},
        {52, 42, 45, 32}, {65, 35, 62, 23}, {80, 33, 80, 20},
        {95, 35, 98, 23}, {108, 42, 115, 32}, {118, 52, 129, 45},
        {125, 69, 137, 65}, {127, 80, 140, 80},
    };
    for (int i = 0; i < 11; ++i) {
        s_ray_points[i][0].x = (lv_value_precise_t)ray[i][0];
        s_ray_points[i][0].y = (lv_value_precise_t)ray[i][1];
        s_ray_points[i][1].x = (lv_value_precise_t)ray[i][2];
        s_ray_points[i][1].y = (lv_value_precise_t)ray[i][3];
        s_rays[i] = line(s_sun_stage, s_ray_points[i], 2, kGold, 1, LV_OPA_TRANSP);
    }

    dot(s_sun_stage, 26, 26, 108, kGold, 14);
    dot(s_sun_stage, 31, 31, 98, kGold, 40);
    dot(s_sun_stage, 42, 42, 76, kGold, LV_OPA_COVER);
    dot(s_sun_stage, 55, 55, 50, 0xF1D68A, LV_OPA_COVER);

    s_horizon = line(s_splash, s_horizon_points,
                     sizeof(s_horizon_points) / sizeof(s_horizon_points[0]),
                     kGold, 1, LV_OPA_TRANSP);
    s_waves[0] = line(s_splash, s_wave_1,
                      sizeof(s_wave_1) / sizeof(s_wave_1[0]),
                      kCyan, 1, LV_OPA_TRANSP);
    s_waves[1] = line(s_splash, s_wave_2,
                      sizeof(s_wave_2) / sizeof(s_wave_2[0]),
                      kGold, 1, LV_OPA_TRANSP);
    s_waves[2] = line(s_splash, s_wave_3,
                      sizeof(s_wave_3) / sizeof(s_wave_3[0]),
                      kCream, 1, LV_OPA_TRANSP);

    s_title = text_line(s_splash, "命海拾遗", &boot_title_24,
                        kCream, 229, 32, 6);
    s_subtitle = text_line(s_splash, "从一日之始，拾起岁月",
                           &buddy_font_16, kDim, 256, 20, 2);
    s_footer = text_line(s_splash, "ALMANAC / WEATHER / VOICE",
                         &lv_font_montserrat_14, kDim, 296, 18, 1);
    set_opacity(s_brand, 0);
    set_opacity(s_title, 0);
    set_opacity(s_subtitle, 0);
    set_opacity(s_footer, 0);

    const int bar_x = 49;
    const int bar_y = 278;
    for (int i = 0; i < kBarN; ++i) {
        s_cells[i] = block(s_splash, bar_x + i * 11, bar_y, 6, 6,
                           kSurface, 150);
    }

    s_step = 0;
    s_animation_done = false;
    lv_timer_t *timer = lv_timer_create(dawn_step, kStageMs, NULL);
    lv_timer_set_repeat_count(timer, kStageN);
    lv_screen_load(s_splash);

    // Push the complete first frame while the backlight is still off.
    const int64_t t0 = esp_timer_get_time();
    lv_refr_now(lv_display_get_default());
    if (io) esp_lcd_panel_io_tx_param(io, 0x00, NULL, 0);
    const int64_t t1 = esp_timer_get_time();
    ESP_LOGI(TAG, "first frame synced: refr=%lldms", (long long)((t1 - t0) / 1000));
    bsp_lvgl_unlock();

    for (int b = 0; b <= 100; b += 10) {
        bsp_display_backlight(b);
        vTaskDelay(pdMS_TO_TICKS(14));
    }

    // Run visual updates on the LVGL task. The original lock-per-frame loop
    // waited on the renderer and stretched the intended 0.9s scene to ~4.4s
    // on the real device.
    while (!s_animation_done) vTaskDelay(pdMS_TO_TICKS(20));

    vTaskDelay(pdMS_TO_TICKS(140));
    ESP_LOGI(TAG, "dawn splash done");
}

void boot_splash_end(void) {
    if (!s_splash) return;
    if (!bsp_lvgl_lock(500)) return;
    lv_obj_delete(s_splash);
    s_splash = NULL;
    bsp_lvgl_unlock();
}
