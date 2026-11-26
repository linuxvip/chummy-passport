#pragma once

#include "apps/remote/remote_controls.h"

#include <cstdint>

namespace remote {

enum class BleState : uint8_t {
    Off = 0,
    Starting,
    Advertising,
    Pairing,
    Connected,
    Error,
};

void ble_start(Mode mode);
void ble_retry();
void ble_stop();
void ble_set_mode(Mode mode);
bool ble_send(Action action);

BleState ble_state();
Mode ble_mode();
bool ble_connected();
uint32_t ble_passkey();
const char *ble_device_name();

}  // namespace remote
