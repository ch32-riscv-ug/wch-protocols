// SPDX-License-Identifier: MIT
#pragma once

#include "oep_usb_test_config.h"

#include <USBVendor.h>

namespace oep_usb_test {

// Keep this object before HID and CDC in Profile B. Arduino-ESP32 3.3.11's
// Microsoft OS 2.0 descriptor binds WinUSB to interface 0.
static USBVendor vendor(64);

inline void begin_vendor() {
  vendor.setRxBufferSize(512);
  vendor.begin();
}

inline void poll_vendor() {
  uint8_t buffer[64];
  const int available = vendor.available();
  if (available <= 0) {
    return;
  }
  const size_t wanted = static_cast<size_t>(available) < sizeof(buffer)
                          ? static_cast<size_t>(available)
                          : sizeof(buffer);
  const size_t received = vendor.read(buffer, wanted);
  if (received != 0) {
    vendor.write(buffer, received);
    vendor.flush();
  }
}

}  // namespace oep_usb_test

