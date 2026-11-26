#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LAUNCHER_STYLE_FLOW = 0,
    LAUNCHER_STYLE_GRID = 1,
    LAUNCHER_STYLE_COUNT
} launcher_style_t;

static inline const char *launcher_style_name(launcher_style_t style) {
    switch (style) {
        case LAUNCHER_STYLE_GRID: return "应用网格";
        default: return "单页轮播";
    }
}

#ifdef __cplusplus
}
#endif
