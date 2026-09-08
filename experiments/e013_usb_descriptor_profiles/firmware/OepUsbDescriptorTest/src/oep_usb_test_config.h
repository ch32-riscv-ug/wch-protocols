// SPDX-License-Identifier: MIT
#pragma once

#include <Arduino.h>

#ifndef OEP_USB_TEST_PROFILE
#error "Define OEP_USB_TEST_PROFILE before including OepUsbDescriptorTest.h"
#endif

#warning "E013 uses private test VID:PID 1209:0001; do not distribute this firmware"

#if OEP_USB_TEST_PROFILE != 1 && OEP_USB_TEST_PROFILE != 2
#error "OEP_USB_TEST_PROFILE must be 1 (HID) or 2 (Vendor + HID + CDC)"
#endif

#ifndef ARDUINO_USB_MODE
#error "This experiment requires an ESP32-S3 with native USB"
#elif ARDUINO_USB_MODE != 0
#error "Select USB-OTG (TinyUSB) mode"
#endif

namespace oep_usb_test {

constexpr uint16_t kTestVid = 0x1209;
constexpr uint16_t kTestPid = 0x0001;
constexpr uint16_t kProfileARevision = 0x0001;
constexpr uint16_t kProfileBRevision = 0x0002;
constexpr char kManufacturer[] = "Open Embedded Probe (TEST ONLY)";
constexpr char kProduct[] = "OEP USB Profile Test";
constexpr char kWebUsbUrl[] = "https://github.com/Open-Embedded-Probe";

constexpr uint16_t profile_revision() {
  return OEP_USB_TEST_PROFILE == 1 ? kProfileARevision : kProfileBRevision;
}

}  // namespace oep_usb_test

