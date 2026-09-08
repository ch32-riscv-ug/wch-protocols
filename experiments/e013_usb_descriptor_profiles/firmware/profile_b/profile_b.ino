// SPDX-License-Identifier: MIT

#define OEP_USB_TEST_PROFILE 2
#include <OepUsbDescriptorTest.h>

void setup() {
  oep_usb_test::begin();
}

void loop() {
  oep_usb_test::poll();
  delay(1);
}

