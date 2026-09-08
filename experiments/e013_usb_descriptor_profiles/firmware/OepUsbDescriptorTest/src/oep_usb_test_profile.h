// SPDX-License-Identifier: MIT
#pragma once

#include "oep_usb_test_config.h"

#include <USB.h>

// Include order fixes interface registration order for Profile B.
#if OEP_USB_TEST_PROFILE == 2
#include "oep_usb_test_vendor.h"
#endif
#include "oep_usb_test_hid.h"
#if OEP_USB_TEST_PROFILE == 2
#include "oep_usb_test_cdc.h"
#endif

namespace oep_usb_test {

inline void begin() {
  USB.VID(kTestVid);
  USB.PID(kTestPid);
  USB.firmwareVersion(profile_revision());
  USB.manufacturerName(kManufacturer);
  USB.productName(kProduct);

#if OEP_USB_TEST_PROFILE == 2
  // Arduino-ESP32 emits WebUSB and Microsoft OS 2.0 descriptors together.
  // USBVendor is interface 0 so Windows can bind WinUSB to the intended child.
  USB.webUSB(true);
  USB.webUSBURL(kWebUsbUrl);
  begin_vendor();
#else
  USB.webUSB(false);
#endif

  begin_hid();
#if OEP_USB_TEST_PROFILE == 2
  begin_cdc();
#endif
  USB.begin();
}

inline void poll() {
#if OEP_USB_TEST_PROFILE == 2
  poll_vendor();
#endif
  poll_hid();
#if OEP_USB_TEST_PROFILE == 2
  poll_cdc();
#endif
}

}  // namespace oep_usb_test

