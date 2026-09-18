#include <Arduino.h>

extern "C" volatile uint32_t e131_marker = 0x11223344u;

void setup() {
  e131_marker = 0xe131b007u;
}

void loop() {
  __asm__ volatile("nop");
}
