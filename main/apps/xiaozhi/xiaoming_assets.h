#pragma once

#include "lvgl.h"

#include <cstdint>

namespace xiaozhi::face::assets {

enum class PortraitFrame : uint8_t {
    Calm,
    Blink,
    Happy,
    Speaking,
    Alert,
    Confident,
    Count,
};

const lv_image_dsc_t *portrait(PortraitFrame frame);
const lv_image_dsc_t *mini_portrait();

}  // namespace xiaozhi::face::assets
