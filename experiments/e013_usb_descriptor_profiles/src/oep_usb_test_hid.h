// SPDX-License-Identifier: MIT
#pragma once

#include "oep_usb_test_config.h"

#include <USBHIDVendor.h>

namespace oep_usb_test {

constexpr size_t kHidPayloadSize = 63;
constexpr uint8_t kHidReportId = 6;  // HID_REPORT_ID_VENDOR in Arduino-ESP32 3.3.11.
static USBHIDVendor hid(kHidPayloadSize, false);

inline void begin_hid() {
  hid.setRxBufferSize(256);
  hid.begin();
}

inline void poll_hid() {
  uint8_t buffer[kHidPayloadSize];
  const int available = hid.available();
  if (available <= 0) {
    return;
  }
  const size_t wanted = static_cast<size_t>(available) < sizeof(buffer)
                          ? static_cast<size_t>(available)
                          : sizeof(buffer);
  const size_t received = hid.read(buffer, wanted);
  if (received != 0) {
    // USBHIDVendor pads this response to one complete input report.
    hid.write(buffer, received);
  }
}

}  // namespace oep_usb_test

