#include "apps/remote/remote_controls.h"

#include <cassert>
#include <string>

int main() {
    using namespace remote;

    assert(action_for(BSP_BTN_UP, BTN_EVT_CLICK) == Action::Previous);
    assert(action_for(BSP_BTN_DOWN, BTN_EVT_CLICK) == Action::Next);
    assert(action_for(BSP_BTN_OK, BTN_EVT_CLICK) == Action::Toggle);
    for (uint8_t button : {BSP_BTN_UP, BSP_BTN_DOWN, BSP_BTN_OK}) {
        assert(action_for(button, BTN_EVT_PRESS) == Action::None);
        assert(action_for(button, BTN_EVT_LONG) == Action::None);
        assert(action_for(button, BTN_EVT_LONG_UP) == Action::None);
        assert(action_for(button, BTN_EVT_RELEASE) == Action::None);
    }

    const KeyboardReport previous =
        keyboard_report(Mode::Presentation, Action::Previous);
    const KeyboardReport next =
        keyboard_report(Mode::Presentation, Action::Next);
    const KeyboardReport toggle =
        keyboard_report(Mode::Presentation, Action::Toggle);
    assert(previous.modifier == 0 && previous.reserved == 0);
    assert(previous.keys[0] == kKeyPageUp);
    assert(next.keys[0] == kKeyPageDown);
    assert(toggle.keys[0] == kKeyF5);
    assert(keyboard_report(Mode::Video, Action::Toggle).keys[0] == 0);
    for (size_t i = 1; i < 6; ++i) {
        assert(previous.keys[i] == 0 && next.keys[i] == 0 && toggle.keys[i] == 0);
    }

    for (Mode mode : {Mode::Video, Mode::Music}) {
        assert(consumer_report(mode, Action::Previous).usage == kConsumerPrevious);
        assert(consumer_report(mode, Action::Next).usage == kConsumerNext);
        assert(consumer_report(mode, Action::Toggle).usage == kConsumerPlayPause);
    }
    assert(consumer_report(Mode::Camera, Action::Toggle).usage ==
           kConsumerVolumeUp);
    assert(consumer_report(Mode::Camera, Action::Previous).usage == 0);
    assert(consumer_report(Mode::Presentation, Action::Toggle).usage == 0);
    assert(consumer_report(Mode::Video, Action::None).usage == 0);

    for (uint8_t value = 0; value < 4; ++value) assert(is_mode(value));
    assert(!is_mode(4));
    assert(cycle_mode(Mode::Video, 1) == Mode::Presentation);
    assert(cycle_mode(Mode::Presentation, 1) == Mode::Camera);
    assert(cycle_mode(Mode::Camera, 1) == Mode::Music);
    assert(cycle_mode(Mode::Music, 1) == Mode::Video);
    assert(cycle_mode(Mode::Video, -1) == Mode::Music);
    assert(cycle_camera_delay(0, 1) == 1);
    assert(cycle_camera_delay(1, -1) == 0);
    assert(cycle_camera_delay(3, 1) == 0);
    assert(cycle_camera_delay(0, -1) == 3);
    assert(std::string(mode_name(Mode::Video)) == "短视频");
    assert(std::string(mode_name(Mode::Presentation)) == "演示");
    assert(std::string(mode_name(Mode::Camera)) == "相机");
    assert(std::string(mode_name(Mode::Music)) == "音乐");
}
