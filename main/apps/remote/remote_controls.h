#pragma once

#include "apps/app_base.h"

#include <cstdint>

namespace remote {

enum class Mode : uint8_t {
    Media = 0,
    Arrows = 1,
};

enum class Action : uint8_t {
    None = 0,
    Previous,
    Next,
    Toggle,
};

struct KeyboardReport {
    uint8_t modifier;
    uint8_t reserved;
    uint8_t keys[6];
};

struct ConsumerReport {
    uint16_t usage;
};

constexpr uint8_t kKeyboardReportId = 1;
constexpr uint8_t kConsumerReportId = 3;
constexpr uint8_t kKeyUpArrow = 0x52;
constexpr uint8_t kKeyDownArrow = 0x51;
constexpr uint8_t kKeySpace = 0x2C;
constexpr uint16_t kConsumerPrevious = 0x00B6;
constexpr uint16_t kConsumerNext = 0x00B5;
constexpr uint16_t kConsumerPlayPause = 0x00CD;

constexpr Action action_for(uint8_t button, app_btn_event_t event) {
    if (event != BTN_EVT_CLICK) return Action::None;
    if (button == BSP_BTN_UP) return Action::Previous;
    if (button == BSP_BTN_DOWN) return Action::Next;
    if (button == BSP_BTN_OK) return Action::Toggle;
    return Action::None;
}

constexpr uint8_t keyboard_usage(Action action) {
    switch (action) {
        case Action::Previous: return kKeyUpArrow;
        case Action::Next: return kKeyDownArrow;
        case Action::Toggle: return kKeySpace;
        case Action::None: return 0;
    }
    return 0;
}

constexpr KeyboardReport keyboard_report(Action action) {
    const uint8_t key = keyboard_usage(action);
    return {
        0,
        0,
        {key, 0, 0, 0, 0, 0},
    };
}

constexpr uint16_t consumer_usage(Action action) {
    switch (action) {
        case Action::Previous: return kConsumerPrevious;
        case Action::Next: return kConsumerNext;
        case Action::Toggle: return kConsumerPlayPause;
        case Action::None: return 0;
    }
    return 0;
}

constexpr ConsumerReport consumer_report(Action action) {
    return {consumer_usage(action)};
}

constexpr bool is_mode(uint8_t value) {
    return value == static_cast<uint8_t>(Mode::Media) ||
           value == static_cast<uint8_t>(Mode::Arrows);
}

constexpr const char *mode_name(Mode mode) {
    switch (mode) {
        case Mode::Media: return "媒体模式";
        case Mode::Arrows: return "方向键模式";
    }
    return "媒体模式";
}

constexpr Mode cycle_mode(Mode mode, int direction) {
    constexpr int kModeCount = 2;
    const int current = static_cast<int>(mode);
    const int next = (current + (direction < 0 ? kModeCount - 1 : 1)) % kModeCount;
    return static_cast<Mode>(next);
}

}  // namespace remote
