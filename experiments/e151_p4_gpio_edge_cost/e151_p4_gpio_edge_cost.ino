// E151: GPIO edge / sample cost on ESP32-P4 by drive method.
// Plan and report: README.ja.md. Pins come from .env via build_config.toml.

#include <Arduino.h>
#include <driver/dedic_gpio.h>
#include <esp_cpu.h>
#include <esp_timer.h>
#include <hal/dedic_gpio_cpu_ll.h>
#include <hal/gpio_ll.h>
#include <soc/gpio_struct.h>

#include "e151_timing.h"

static int pinA = 0;
static int pinB = 0;
static dedic_gpio_bundle_handle_t outBundle = nullptr;
static dedic_gpio_bundle_handle_t inBundle = nullptr;

static void report(const char *kind, const char *method, int pin, Timing t,
                   const char *unit) {
  const double ns = t.total_us * 1000.0 / t.count;
  Serial.printf("%s method=%s pin=%d %s=%lu total_us=%lld ns_per_%s=%.1f\n",
                kind, method, pin, kind[0] == 'B' ? "bits" : kind[0] == 'R' ? "samples" : "edges",
                (unsigned long)t.count, (long long)t.total_us, unit, ns);
}

static volatile uint32_t sink = 0;

static void measureAll() {
  // ---- edges (set/clear alternating) ----
  pinMode(pinA, OUTPUT);
  report("EDGE", "digitalWrite", pinA,
         e151_timed(20000, false, [](uint32_t i) { digitalWrite(pinA, i & 1); }), "edge");
  report("EDGE", "gpio_ll", pinA,
         e151_timed(200000, true, [](uint32_t i) { gpio_ll_set_level(&GPIO, pinA, i & 1); }), "edge");
  // dedicated GPIO: bundle bit 0 = pinA, bit 1 = pinB
  report("EDGE", "dedic", pinA,
         e151_timed(200000, true, [](uint32_t i) { dedic_gpio_cpu_ll_write_mask(0x1, i & 1); }), "edge");

  // ---- reads ----
  pinMode(pinB, INPUT_PULLUP);
  report("READ", "digitalRead", pinB,
         e151_timed(20000, false, [](uint32_t) { sink += digitalRead(pinB); }), "sample");
  report("READ", "gpio_ll", pinB,
         e151_timed(200000, true, [](uint32_t) { sink += gpio_ll_get_level(&GPIO, pinB); }), "sample");
  report("READ", "dedic", pinB,
         e151_timed(200000, true, [](uint32_t) { sink += dedic_gpio_cpu_ll_read_in(); }), "sample");

  // ---- one RVSWD-shaped bit: CLK low, DIO = bit, CLK high ----
  pinMode(pinA, OUTPUT);
  pinMode(pinB, OUTPUT);
  report("BIT", "digitalWrite", pinA,
         e151_timed(20000, false, [](uint32_t i) {
           digitalWrite(pinA, LOW); digitalWrite(pinB, i & 1); digitalWrite(pinA, HIGH);
         }), "bit");
  report("BIT", "gpio_ll", pinA,
         e151_timed(200000, true, [](uint32_t i) {
           gpio_ll_set_level(&GPIO, pinA, 0); gpio_ll_set_level(&GPIO, pinB, i & 1);
           gpio_ll_set_level(&GPIO, pinA, 1);
         }), "bit");
  report("BIT", "dedic", pinA,
         e151_timed(200000, true, [](uint32_t i) {
           dedic_gpio_cpu_ll_write_mask(0x1, 0); dedic_gpio_cpu_ll_write_mask(0x2, (i & 1) << 1);
           dedic_gpio_cpu_ll_write_mask(0x1, 1);
         }), "bit");
  pinMode(pinA, INPUT);
  pinMode(pinB, INPUT);
}

void setup() {
  Serial.begin(115200);
  pinA = atoi(PIN_A_STR);
  pinB = atoi(PIN_B_STR);
}

void loop() {
  if (!Serial.available()) return;
  const int c = Serial.read();
  if (c != 'R') return;
  Serial.printf("# EXP E151 v1 git=%s probe=esp32p4_x035 target=none pins=%d,%d cpu_mhz=%lu build=%s\n",
                BANNER_GIT, pinA, pinB, (unsigned long)getCpuFrequencyMhz(), __DATE__ " " __TIME__);
  if (pinA == pinB || pinA < 0 || pinB < 0) { Serial.println("ERROR pins"); return; }
  // Dedicated GPIO bundles are created once; dedic reads see the same pinB.
  if (!outBundle) {
    const int outPins[] = {pinA, pinB};
    dedic_gpio_bundle_config_t outCfg = {};
    outCfg.gpio_array = outPins; outCfg.array_size = 2;
    outCfg.flags.out_en = 1;
    if (dedic_gpio_new_bundle(&outCfg, &outBundle) != ESP_OK) { Serial.println("ERROR dedic out"); return; }
  }
  if (!inBundle) {
    const int inPins[] = {pinB};
    dedic_gpio_bundle_config_t inCfg = {};
    inCfg.gpio_array = inPins; inCfg.array_size = 1;
    inCfg.flags.in_en = 1;
    if (dedic_gpio_new_bundle(&inCfg, &inBundle) != ESP_OK) { Serial.println("ERROR dedic in"); return; }
  }
  for (int rep = 1; rep <= 3; ++rep) {
    Serial.printf("REP %d\n", rep);
    measureAll();
  }
  Serial.println("MEASURE END");
}
