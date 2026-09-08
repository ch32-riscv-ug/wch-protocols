// SPDX-License-Identifier: MIT
#pragma once

#include "oep_usb_test_config.h"

#include <USBCDC.h>

namespace oep_usb_test {

static USBCDC cdc;

inline void begin_cdc() {
  cdc.setRxBufferSize(512);
  cdc.setTxTimeoutMs(100);
  cdc.begin(115200);
}

inline void poll_cdc() {
  uint8_t buffer[64];
  const int available = cdc.available();
  if (available <= 0) {
    return;
  }
  const size_t wanted = static_cast<size_t>(available) < sizeof(buffer)
                          ? static_cast<size_t>(available)
                          : sizeof(buffer);
  const size_t received = cdc.read(buffer, wanted);
  if (received != 0) {
    cdc.write(buffer, received);
    cdc.flush();
  }
}

}  // namespace oep_usb_test

