// SPDX-License-Identifier: MIT

#include "src/oep_usb_test_profile.h"

void setup() {
  oep_usb_test::begin();
}

void loop() {
  oep_usb_test::poll();
  delay(1);
}

