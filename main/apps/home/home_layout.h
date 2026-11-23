#pragma once

typedef enum {
    HOME_LAYOUT_ALMANAC = 0,
    HOME_LAYOUT_MINIMAL = 1,
    HOME_LAYOUT_WEATHER = 2,
    HOME_LAYOUT_PROFILE = 3,
    HOME_LAYOUT_COUNT,
} home_layout_t;

static inline const char *home_layout_name(home_layout_t layout) {
    switch (layout) {
        case HOME_LAYOUT_MINIMAL: return "极简四柱";
        case HOME_LAYOUT_WEATHER: return "天气生活";
        case HOME_LAYOUT_PROFILE: return "人物志";
        default: return "命海拾遗";
    }
}
