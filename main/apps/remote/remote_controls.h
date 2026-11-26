#pragma once

#include "apps/app_base.h"

#include <cstdint>

namespace remote {

enum class Mode : uint8_t {
    Video = 0,
    Presentation = 1,
    Camera = 2,
    Music = 3,
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
constexpr uint8_t kKeyPageUp = 0x4B;
constexpr uint8_t kKeyPageDown = 0x4E;
constexpr uint8_t kKeyF5 = 0x3E;
constexpr uint16_t kConsumerPrevious = 0x00B6;
constexpr uint16_t kConsumerNext = 0x00B5;
constexpr uint16_t kConsumerPlayPause = 0x00CD;
constexpr uint16_t kConsumerVolumeUp = 0x00E9;

constexpr Action action_for(uint8_t button, app_btn_event_t event) {
    if (event != BTN_EVT_CLICK) return Action::None;
    if (button == BSP_BTN_UP) return Action::Previous;
    if (button == BSP_BTN_DOWN) return Action::Next;
    if (button == BSP_BTN_OK) return Action::Toggle;
    return Action::None;
}

constexpr uint8_t keyboard_usage(Mode mode, Action action) {
    if (mode == Mode::Presentation) {
        switch (action) {
            case Action::Previous: return kKeyPageUp;
            case Action::Next: return kKeyPageDown;
            case Action::Toggle: return kKeyF5;
            case Action::None: return 0;
        }
    }
    return 0;
}

constexpr KeyboardReport keyboard_report(Mode mode, Action action) {
    const uint8_t key = keyboard_usage(mode, action);
    return {
        0,
        0,
        {key, 0, 0, 0, 0, 0},
    };
}

constexpr uint16_t consumer_usage(Mode mode, Action action) {
    if (mode == Mode::Camera)
        return action == Action::Toggle ? kConsumerVolumeUp : 0;
    if (mode == Mode::Video || mode == Mode::Music) {
        switch (action) {
            case Action::Previous: return kConsumerPrevious;
            case Action::Next: return kConsumerNext;
            case Action::Toggle: return kConsumerPlayPause;
            case Action::None: return 0;
        }
    }
    return 0;
}

constexpr ConsumerReport consumer_report(Mode mode, Action action) {
    return {consumer_usage(mode, action)};
}

constexpr bool is_mode(uint8_t value) {
    return value <= static_cast<uint8_t>(Mode::Music);
}

constexpr const char *mode_name(Mode mode) {
    switch (mode) {
        case Mode::Video: return "短视频";
        case Mode::Presentation: return "演示";
        case Mode::Camera: return "相机";
        case Mode::Music: return "音乐";
    }
    return "短视频";
}

constexpr Mode cycle_mode(Mode mode, int direction) {
    constexpr int kModeCount = 4;
    const int current = static_cast<int>(mode);
    const int next = (current + (direction < 0 ? kModeCount - 1 : 1)) % kModeCount;
    return static_cast<Mode>(next);
}

constexpr uint8_t cycle_camera_delay(uint8_t index, int direction) {
    constexpr int kDelayCount = 4;
    const int next = (static_cast<int>(index) +
                      (direction < 0 ? kDelayCount - 1 : 1)) % kDelayCount;
    return static_cast<uint8_t>(next);
}

}  // namespace remote
