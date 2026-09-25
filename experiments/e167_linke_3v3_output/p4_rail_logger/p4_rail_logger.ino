// p4_rail_logger: record a WCH-LinkE's 3V3 output (ADC) and its debug pins (digital) on an ESP32-P4.
// Every pin is an input; nothing is driven. Pins (E167 wiring): 16 RST, 17 3V3, 19 SWDIO, 20 SWCLK, 21 3V3, 23 RX, 39 TX.
// GND is on 18/22 and must never be configured as an output.
//
// Serial (USB-Serial/JTAG, 921600): "r<ms>\n" records for <ms> ms, then prints
//   "# n=<samples> us=<duration>" and CSV lines "t_us,mv17,mv21,rst,dio,clk,rx,tx", one per change (>= 40 mV or a
//   digital edge) plus at least one per millisecond, and "# end".
#include <Arduino.h>

struct Sample { uint32_t t; uint16_t a17, a21; uint8_t bits; };
static Sample *buf;
static const uint32_t MAXN = 1500000;   // ~12 MB in PSRAM

static inline uint8_t digital_bits() {
  return (digitalRead(16) << 0) | (digitalRead(19) << 1) | (digitalRead(20) << 2) | (digitalRead(23) << 3) | (digitalRead(39) << 4);
}

void setup() {
  Serial.begin(921600);
  for (int p : {16, 19, 20, 23, 39}) pinMode(p, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(17, ADC_11db);
  analogSetPinAttenuation(21, ADC_11db);
  buf = (Sample *)ps_malloc(sizeof(Sample) * MAXN);
}

void loop() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  if (!cmd.startsWith("r") || !buf) { Serial.println(buf ? "# ? use r<ms>" : "# no psram"); return; }
  uint32_t ms = cmd.substring(1).toInt(), n = 0, t0 = micros();
  while (n < MAXN && micros() - t0 < ms * 1000u) {
    buf[n].t = micros() - t0;
    buf[n].a17 = analogReadMilliVolts(17);
    buf[n].a21 = analogReadMilliVolts(21);
    buf[n].bits = digital_bits();
    n++;
  }
  Serial.printf("# n=%u us=%u\n", n, micros() - t0);
  int last17 = -1000, last21 = -1000, lastb = -1; uint32_t lastt = 0;
  for (uint32_t i = 0; i < n; i++) {
    const Sample &s = buf[i];
    if (abs((int)s.a17 - last17) >= 40 || abs((int)s.a21 - last21) >= 40 || s.bits != lastb || s.t - lastt >= 1000 || i == n - 1) {
      Serial.printf("%u,%u,%u,%u,%u,%u,%u,%u\n", s.t, s.a17, s.a21, s.bits & 1, (s.bits >> 1) & 1, (s.bits >> 2) & 1, (s.bits >> 3) & 1, (s.bits >> 4) & 1);
      last17 = s.a17; last21 = s.a21; lastb = s.bits; lastt = s.t;
    }
  }
  Serial.println("# end");
}
