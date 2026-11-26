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

    const KeyboardReport previous = keyboard_report(Action::Previous);
    const KeyboardReport next = keyboard_report(Action::Next);
    const KeyboardReport toggle = keyboard_report(Action::Toggle);
    assert(previous.modifier == 0 && previous.reserved == 0);
    assert(previous.keys[0] == kKeyUpArrow);
    assert(next.keys[0] == kKeyDownArrow);
    assert(toggle.keys[0] == kKeySpace);
    for (size_t i = 1; i < 6; ++i) {
        assert(previous.keys[i] == 0 && next.keys[i] == 0 && toggle.keys[i] == 0);
    }

    assert(consumer_report(Action::Previous).usage == kConsumerPrevious);
    assert(consumer_report(Action::Next).usage == kConsumerNext);
    assert(consumer_report(Action::Toggle).usage == kConsumerPlayPause);
    assert(consumer_report(Action::None).usage == 0);

    assert(is_mode(static_cast<uint8_t>(Mode::Media)));
    assert(is_mode(static_cast<uint8_t>(Mode::Arrows)));
    assert(!is_mode(2));
    assert(cycle_mode(Mode::Media, 1) == Mode::Arrows);
    assert(cycle_mode(Mode::Arrows, 1) == Mode::Media);
    assert(cycle_mode(Mode::Media, -1) == Mode::Arrows);
    assert(std::string(mode_name(Mode::Media)) == "媒体模式");
    assert(std::string(mode_name(Mode::Arrows)) == "方向键模式");
}
