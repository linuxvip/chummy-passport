// Xiaozhi companion face. A 144px illustrated portrait is centred in the
// 160x152 host; lightweight LVGL shapes add listening, thinking, sleep and
// real microphone amplitude effects.
#include "apps/xiaozhi/xiaozhi_face.h"
#include "apps/xiaozhi/xiaoming_assets.h"

#include <cstring>

namespace xiaozhi::face {

namespace {

constexpr uint16_t kMoodAfterglowFrames = 6000 / kFramePeriodMs;
constexpr uint8_t kBlinkOpen[] = {100, 55, 10, 10, 55};  // indexed by frames into a blink

struct Pen {
    lv_layer_t *layer;
    int32_t ox, oy;

    void rect(int x, int y, int w, int h, uint32_t color, int radius = 0, lv_opa_t opa = LV_OPA_COVER) {
        if (w <= 0 || h <= 0) return;
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = lv_color_hex(color);
        d.bg_opa = opa;
        d.radius = radius;
        const lv_area_t a{ox + x, oy + y, ox + x + w - 1, oy + y + h - 1};
        lv_draw_rect(layer, &d, &a);
    }
    void panel(int x, int y, int w, int h, uint32_t fill, uint32_t edge, int radius, int border) {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = lv_color_hex(fill);
        d.bg_opa = LV_OPA_COVER;
        d.border_color = lv_color_hex(edge);
        d.border_width = border;
        d.border_opa = LV_OPA_COVER;
        d.radius = radius;
        const lv_area_t a{ox + x, oy + y, ox + x + w - 1, oy + y + h - 1};
        lv_draw_rect(layer, &d, &a);
    }
    void ring(int x, int y, int w, int h, uint32_t color, int radius, int width) {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_opa = LV_OPA_TRANSP;
        d.border_color = lv_color_hex(color);
        d.border_width = width;
        d.border_opa = LV_OPA_COVER;
        d.radius = radius;
        const lv_area_t a{ox + x, oy + y, ox + x + w - 1, oy + y + h - 1};
        lv_draw_rect(layer, &d, &a);
    }
    // Angles: 0 = 3 o'clock, 90 = 6 o'clock, drawn clockwise.
    void arc(int cx, int cy, int radius, int width, int start, int end, uint32_t color,
             lv_opa_t opa = LV_OPA_COVER) {
        lv_draw_arc_dsc_t d;
        lv_draw_arc_dsc_init(&d);
        d.color = lv_color_hex(color);
        d.width = width;
        d.start_angle = start;
        d.end_angle = end;
        d.center.x = ox + cx;
        d.center.y = oy + cy;
        d.radius = static_cast<uint16_t>(radius);
        d.opa = opa;
        d.rounded = 1;
        lv_draw_arc(layer, &d);
    }
    void tri(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color) {
        lv_draw_triangle_dsc_t d;
        lv_draw_triangle_dsc_init(&d);
        d.p[0].x = ox + x0; d.p[0].y = oy + y0;
        d.p[1].x = ox + x1; d.p[1].y = oy + y1;
        d.p[2].x = ox + x2; d.p[2].y = oy + y2;
        d.color = lv_color_hex(color);
        d.opa = LV_OPA_COVER;
        lv_draw_triangle(layer, &d);
    }
    void line(int x0, int y0, int x1, int y1, int width, uint32_t color, lv_opa_t opa = LV_OPA_COVER) {
        lv_draw_line_dsc_t d;
        lv_draw_line_dsc_init(&d);
        d.p1.x = ox + x0; d.p1.y = oy + y0;
        d.p2.x = ox + x1; d.p2.y = oy + y1;
        d.width = width;
        d.color = lv_color_hex(color);
        d.opa = opa;
        d.round_start = 1;
        d.round_end = 1;
        lv_draw_line(layer, &d);
    }
};

int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

constexpr int kCx = kBigW / 2;

assets::PortraitFrame portrait_frame(const Pose &p) {
    if (p.activity == Activity::Error) return assets::PortraitFrame::Alert;
    if (p.activity == Activity::Thinking || p.activity == Activity::Connecting)
        return assets::PortraitFrame::Confident;
    if (p.activity == Activity::Speaking) {
        return p.mouth >= 4 ? assets::PortraitFrame::Speaking
                            : assets::PortraitFrame::Calm;
    }
    switch (p.mood) {
    case Mood::Happy:
    case Mood::Laugh:
    case Mood::Love:
    case Mood::Wink:
    case Mood::Shy:
        return assets::PortraitFrame::Happy;
    case Mood::Sad:
    case Mood::Cry:
    case Mood::Angry:
    case Mood::Surprised:
        return assets::PortraitFrame::Alert;
    case Mood::Cool:
    case Mood::Confused:
        return assets::PortraitFrame::Confident;
    case Mood::Sleepy:
        return assets::PortraitFrame::Blink;
    default:
        return p.open < 65 ? assets::PortraitFrame::Blink
                           : assets::PortraitFrame::Calm;
    }
}

void draw_portrait(lv_layer_t *layer, const lv_area_t &area, const Pose &p) {
    lv_draw_image_dsc_t d;
    lv_draw_image_dsc_init(&d);
    d.src = assets::portrait(portrait_frame(p));
    d.opa = LV_OPA_COVER;
    const int dx = 8 + p.gaze_x / 3;
    const int dy = 4 + p.bob + p.gaze_y / 3;
    const lv_area_t target{
        area.x1 + dx, area.y1 + dy,
        area.x1 + dx + 143, area.y1 + dy + 143,
    };
    lv_draw_image(layer, &d, &target);
}

void heart(Pen &pen, int cx, int cy, int s, uint32_t color, uint32_t glint) {
    const int r = 4 + s;
    pen.rect(cx - 4 - r, cy - 2 - r, 2 * r, 2 * r, color, r);
    pen.rect(cx + 4 - r, cy - 2 - r, 2 * r, 2 * r, color, r);
    pen.tri(cx - 9 - s, cy, cx + 9 + s, cy, cx, cy + 9 + s, color);
    pen.rect(cx - 7, cy - 6, 3, 3, glint, 2);
}

void draw_letter_z(Pen &pen, int x, int y, int s, uint32_t color, lv_opa_t opa) {
    pen.line(x, y, x + s, y, 2, color, opa);
    pen.line(x + s, y, x, y + s, 2, color, opa);
    pen.line(x, y + s, x + s, y + s, 2, color, opa);
}

void draw_extras(Pen &pen, const Pose &p, int bob, const Palette &c) {
    switch (p.activity) {
    case Activity::Listening: {
        // Sound ripples beside the ears, stepping outward.
        const int step = (p.phase >> 1) % 3;
        for (int i = 0; i < 3; ++i) {
            const lv_opa_t opa = i == step ? LV_OPA_COVER : LV_OPA_30;
            const int r = 53 + i * 4;
            pen.arc(kCx, 55 + bob, r, 2, 346, 14, c.tear, opa);
            pen.arc(kCx, 55 + bob, r, 2, 166, 194, c.tear, opa);
        }
        break;
    }
    case Activity::Thinking: {
        const int shown = (p.phase >> 2) & 3;
        if (shown > 0) pen.rect(94, 15, 5, 5, c.warn, 3);
        if (shown > 1) pen.rect(101, 8, 7, 7, c.warn, 4);
        if (shown > 2) pen.rect(110, 1, 10, 10, c.warn, 5);
        break;
    }
    case Activity::Offline: {
        const int rise = p.phase >> 2;
        draw_letter_z(pen, 92, 14 - rise, 6, c.edge, LV_OPA_COVER);
        draw_letter_z(pen, 103, 6 - rise / 2, 8, c.edge, rise > 2 ? LV_OPA_50 : LV_OPA_COVER);
        break;
    }
    case Activity::Error: {
        const int slide = (p.phase & 7) / 2;
        pen.tri(100, 23 + slide + bob, 96, 31 + slide + bob, 104, 31 + slide + bob, c.tear);
        pen.rect(96, 27 + slide + bob, 9, 9, c.tear, 5);
        break;
    }
    default: break;
    }
}

}  // namespace

Mood mood_from_emotion(const char *e) {
    if (!e) return Mood::Neutral;
    struct Entry { const char *name; Mood mood; };
    static constexpr Entry kTable[] = {
        {"happy", Mood::Happy},       {"relaxed", Mood::Happy},     {"confident", Mood::Happy},
        {"delicious", Mood::Happy},   {"funny", Mood::Laugh},       {"silly", Mood::Laugh},
        {"laughing", Mood::Laugh},    {"sad", Mood::Sad},           {"crying", Mood::Cry},
        {"angry", Mood::Angry},       {"surprised", Mood::Surprised}, {"shocked", Mood::Surprised},
        {"loving", Mood::Love},       {"kissy", Mood::Love},        {"winking", Mood::Wink},
        {"cool", Mood::Cool},         {"sleepy", Mood::Sleepy},     {"thinking", Mood::Confused},
        {"confused", Mood::Confused}, {"embarrassed", Mood::Shy},
    };
    for (const auto &entry : kTable) {
        if (std::strcmp(e, entry.name) == 0) return entry.mood;
    }
    return Mood::Neutral;
}

uint32_t Animator::random() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

void Animator::set_mood(Mood mood) {
    mood_ = mood;
    mood_ttl_ = kMoodAfterglowFrames;
}

Pose Animator::step(Activity a) {
    ++tick_;
    if (a != last_) {
        last_ = a;
        if (a == Activity::Listening || a == Activity::Connecting || a == Activity::Offline ||
            a == Activity::Error) mood_ = Mood::Neutral;
        glance_at_ = tick_;
    }
    if (a == Activity::Idle && mood_ != Mood::Neutral && mood_ttl_ && --mood_ttl_ == 0) mood_ = Mood::Neutral;

    // Where the eyes want to look.
    switch (a) {
    case Activity::Thinking:
        target_x_ = (tick_ / 12) & 1 ? 4 : 5;
        target_y_ = -4;
        break;
    case Activity::Connecting: {
        const int t = tick_ % 32;
        target_x_ = static_cast<int8_t>((t < 16 ? t : 32 - t) * 10 / 16 - 5);
        target_y_ = 0;
        break;
    }
    case Activity::Listening: target_x_ = 0; target_y_ = 0; break;
    case Activity::Offline:   target_x_ = 0; target_y_ = 2; break;
    case Activity::Error:     target_x_ = 0; target_y_ = 1; break;
    default:
        if (tick_ >= glance_at_) {
            static constexpr int8_t kGlances[][2] = {
                {0, 0}, {0, 0}, {0, 0}, {-5, 0}, {5, 0}, {-4, -3}, {4, -3}, {0, 2}, {-3, 1}, {3, 1},
            };
            const auto &g = kGlances[random() % (sizeof(kGlances) / sizeof(kGlances[0]))];
            const bool calm = a == Activity::Speaking;
            target_x_ = calm ? g[0] / 2 : g[0];
            target_y_ = calm ? g[1] / 2 : g[1];
            glance_at_ = tick_ + 30 + random() % 60;
        }
        if (mood_ == Mood::Shy) target_y_ = 3;
        break;
    }
    // Quick saccade: up to 3 px per frame.
    gaze_x_ = static_cast<int8_t>(gaze_x_ + clamp(target_x_ - gaze_x_, -3, 3));
    gaze_y_ = static_cast<int8_t>(gaze_y_ + clamp(target_y_ - gaze_y_, -3, 3));

    // Blink every 2.5-6 s, sometimes twice.
    uint8_t open = 100;
    if (a != Activity::Offline) {
        if (!blink_ && tick_ >= blink_at_) blink_ = 1;
        if (blink_) {
            open = kBlinkOpen[blink_];
            if (++blink_ >= sizeof(kBlinkOpen)) {
                blink_ = 0;
                if (blink_twice_) {
                    blink_twice_ = false;
                    blink_at_ = tick_ + 3;
                } else {
                    blink_twice_ = random() % 4 == 0;
                    blink_at_ = tick_ + 50 + random() % 70;
                }
            }
        }
    }

    if (a == Activity::Speaking) {
        if (tick_ % 2 == 0) mouth_ = random() % 6 == 0 ? 2 : static_cast<uint8_t>(3 + random() % 8);
    } else {
        mouth_ = 0;
    }

    static constexpr int8_t kBob[] = {0, 0, 1, 2, 2, 1};
    int8_t bob = 0;
    uint8_t phase = 0;
    switch (a) {
    case Activity::Idle:       bob = kBob[(tick_ / 8) % 6]; break;
    case Activity::Speaking:
        bob = kBob[(tick_ / 4) % 6];
        phase = (tick_ / 2) & 15;
        break;
    case Activity::Thinking:   bob = kBob[(tick_ / 10) % 6]; phase = (tick_ / 5) & 15; break;
    case Activity::Connecting: bob = kBob[(tick_ / 6) % 6]; phase = (tick_ / 4) & 15; break;
    case Activity::Offline:    bob = kBob[(tick_ / 14) % 6]; phase = (tick_ / 5) & 15; break;
    case Activity::Listening:  phase = (tick_ / 3) & 15; break;
    case Activity::Error:      phase = (tick_ / 3) & 15; break;
    }
    if (mood_ == Mood::Love || mood_ == Mood::Cry) phase = (tick_ / 2) & 15;

    return Pose{a, mood_, bob, gaze_x_, gaze_y_, open, mouth_, phase};
}

void draw_big(lv_layer_t *layer, const lv_area_t &area, const Pose &p, const Palette &c) {
    draw_portrait(layer, area, p);
    Pen pen{layer, area.x1, area.y1};
    const int b = p.bob;

    // A compact live waveform sits across the jacket, replacing the old robot
    // visor while preserving the real microphone feedback.
    pen.rect(53, 119 + b, 54, 9, c.outfit_dark, 5, LV_OPA_80);
    for (int i = 0; i < kWaveBars; ++i) {
        const int height = 2 + (p.bars[i] * 5 + 50) / 100;
        pen.rect(57 + i * 3, 124 - height / 2, 2, height,
                 p.bars[i] ? c.accessory : c.hair_light, 2,
                 p.bars[i] ? LV_OPA_COVER : LV_OPA_40);
    }

    if (p.mood == Mood::Love) {
        const int sway = (p.phase & 3) * 2;
        heart(pen, 126 - sway, 35 + b, 2, c.blush, c.glint);
        heart(pen, 138 - sway, 21 + b, 1, c.blush, c.glint);
    }
    draw_extras(pen, p, b, c);
}

void draw_mini(lv_layer_t *layer, const lv_area_t &area, const Pose &p, const Palette &c, uint32_t frame) {
    Pen pen{layer, area.x1, area.y1};
    (void)p;
    pen.panel(0, 0, kMiniW, kMiniH, c.screen, frame, 7, 2);
    lv_draw_image_dsc_t d;
    lv_draw_image_dsc_init(&d);
    d.src = assets::mini_portrait();
    d.clip_radius = 5;
    const lv_area_t target{
        area.x1 + 1, area.y1 + 1,
        area.x1 + kMiniW - 1, area.y1 + kMiniH - 1,
    };
    lv_draw_image(layer, &d, &target);
}

}  // namespace xiaozhi::face
