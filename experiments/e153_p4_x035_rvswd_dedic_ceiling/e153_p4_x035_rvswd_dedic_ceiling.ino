// E153: RVSWD clock ceiling against a CH32X035 with dedicated-GPIO bit-bang.
// Read-only toward the target: only DMCONTROL.dmactive and DMDATA0 are written.
// Plan and report: README.ja.md

#include <Arduino.h>
#include <driver/dedic_gpio.h>
#include <driver/gpio.h>
#include <esp_cpu.h>
#include <esp_timer.h>
#include <hal/dedic_gpio_cpu_ll.h>
#include <hal/gpio_ll.h>
#include <soc/gpio_struct.h>

#include "e153_types.h"

static int gDio = 2, gClk = 54;
static uint32_t gHalfCycles = 0;
static bool gPushPull = false;
static uint32_t gCpuMhz = 360;
static dedic_gpio_bundle_handle_t gOut = nullptr;  // bit0 = DIO, bit1 = CLK
static dedic_gpio_bundle_handle_t gIn = nullptr;   // bit0 = DIO

static inline void spin() {
  if (!gHalfCycles) return;
  const uint32_t start = esp_cpu_get_cycle_count();
  while (esp_cpu_get_cycle_count() - start < gHalfCycles) {}
}
static inline void clkLowDio(bool v) { dedic_gpio_cpu_ll_write_mask(0x3, v ? 0x1 : 0x0); }
static inline void clkHigh() { dedic_gpio_cpu_ll_write_mask(0x2, 0x2); }
static inline void clk(bool v) { dedic_gpio_cpu_ll_write_mask(0x2, v ? 0x2 : 0); }
static inline void dio(bool v) { dedic_gpio_cpu_ll_write_mask(0x1, v ? 0x1 : 0); }
static inline bool dioRead() { return dedic_gpio_cpu_ll_read_in() & 0x1; }
static inline void dioHostDrives(bool yes) {
  if (!gPushPull) return;
  if (yes) gpio_ll_output_enable(&GPIO, gDio); else gpio_ll_output_disable(&GPIO, gDio);
}

static void releaseBus() {
  gpio_ll_output_disable(&GPIO, gDio);
  gpio_ll_output_disable(&GPIO, gClk);
}

static void configureBus() {
  if (gPushPull) gpio_ll_od_disable(&GPIO, gDio); else gpio_ll_od_enable(&GPIO, gDio);
  gpio_ll_pullup_en(&GPIO, gDio);
  gpio_ll_input_enable(&GPIO, gDio);
  gpio_set_drive_capability(gpio_num_t(gDio), GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability(gpio_num_t(gClk), GPIO_DRIVE_CAP_0);
  dedic_gpio_cpu_ll_write_mask(0x3, 0x3);
  gpio_ll_output_enable(&GPIO, gClk);
  gpio_ll_output_enable(&GPIO, gDio);
  delayMicroseconds(20);
  for (int i = 0; i < 100; ++i) { clkLowDio(true); spin(); clkHigh(); spin(); }
  clkLowDio(false); spin(); clkHigh(); spin(); dio(true);
  delayMicroseconds(20);
}

static inline void clockBit(bool v) { clkLowDio(v); spin(); clkHigh(); spin(); }
static inline bool readBit() {
  if (gPushPull) clk(false); else clkLowDio(true);
  spin(); const bool v = dioRead(); clkHigh(); spin(); return v;
}
static inline void startFrame() { dedic_gpio_cpu_ll_write_mask(0x3, 0x3); spin(); dio(false); spin(); }
static inline void stopFrame() { clockBit(false); dedic_gpio_cpu_ll_write_mask(0x3, 0x3); spin(); }
static inline void header(uint8_t address, bool write) {
  bool parity = write;
  for (int bit = 6; bit >= 0; --bit) { const bool v = (address >> bit) & 1; parity ^= v; clockBit(v); }
  clockBit(write); clockBit(parity);
}
static inline void aux(uint8_t pattern) { for (int b = 4; b >= 0; --b) clockBit((pattern >> b) & 1); }

static Reply readDmi(uint8_t address) {
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  portENTER_CRITICAL(&mux);
  startFrame(); header(address, false); aux(0x15);
  dioHostDrives(false);
  Reply r = {0, false}; bool parity = false;
  for (int bit = 31; bit >= 0; --bit) { const bool v = readBit(); r.data |= uint32_t(v) << bit; parity ^= v; }
  r.parity_ok = readBit() == parity;
  dioHostDrives(true);
  aux(0x17); stopFrame();
  portEXIT_CRITICAL(&mux);
  spin(); spin();
  return r;
}

static void writeDmi(uint8_t address, uint32_t data) {
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  portENTER_CRITICAL(&mux);
  startFrame(); header(address, true); aux(0x15);
  bool parity = false;
  for (int bit = 31; bit >= 0; --bit) { const bool v = (data >> bit) & 1; parity ^= v; clockBit(v); }
  clockBit(parity); aux(0x17); stopFrame();
  portEXIT_CRITICAL(&mux);
  spin(); spin();
}

static void sweepPoint(bool pushPull, uint32_t halfNs) {
  gPushPull = pushPull;
  gHalfCycles = (uint32_t)((uint64_t)halfNs * gCpuMhz / 1000);
  releaseBus();
  delay(2);
  configureBus();
  writeDmi(0x10, 1); writeDmi(0x10, 1);
  uint32_t parityOk = 0, valueMatch = 0, first = 0;
  const int64_t t0 = esp_timer_get_time();
  for (int i = 0; i < 1000; ++i) {
    const Reply r = readDmi(0x11);
    if (i == 0) first = r.data;
    parityOk += r.parity_ok; valueMatch += (r.data == first);
  }
  const int64_t t1 = esp_timer_get_time();
  uint32_t ctrlOk = 0;
  for (int i = 0; i < 200; ++i) { const Reply r = readDmi(0x10); ctrlOk += (r.parity_ok && r.data == 1); }
  uint32_t writeOk = 0;
  for (int i = 0; i < 200; ++i) {
    const uint32_t pattern = 0xa5000000u ^ (uint32_t(i) * 0x01010101u);
    writeDmi(0x04, pattern);
    const Reply r = readDmi(0x04);
    writeOk += (r.parity_ok && r.data == pattern);
  }
  writeDmi(0x10, 0);
  releaseBus();
  Serial.printf("SWEEP mode=%s half_ns=%lu half_cycles=%lu reads=1000 parity_ok=%lu value_match=%lu dmstatus=0x%08lx "
                "ctrl_reads=200 ctrl_ok=%lu writes=200 write_ok=%lu ns_per_read=%.0f\n",
                pushPull ? "pp" : "od", (unsigned long)halfNs, (unsigned long)gHalfCycles,
                (unsigned long)parityOk, (unsigned long)valueMatch, (unsigned long)first,
                (unsigned long)ctrlOk, (unsigned long)writeOk, (t1 - t0) * 1000.0 / 1000.0);
}

static bool setupBundles() {
  if (gOut && gIn) return true;
  pinMode(gDio, OUTPUT_OPEN_DRAIN | PULLUP);
  pinMode(gClk, OUTPUT);
  const int outPins[] = {gDio, gClk};
  dedic_gpio_bundle_config_t outCfg = {};
  outCfg.gpio_array = outPins; outCfg.array_size = 2; outCfg.flags.out_en = 1;
  if (dedic_gpio_new_bundle(&outCfg, &gOut) != ESP_OK) return false;
  const int inPins[] = {gDio};
  dedic_gpio_bundle_config_t inCfg = {};
  inCfg.gpio_array = inPins; inCfg.array_size = 1; inCfg.flags.in_en = 1;
  if (dedic_gpio_new_bundle(&inCfg, &gIn) != ESP_OK) return false;
  dedic_gpio_cpu_ll_write_mask(0x3, 0x3);
  releaseBus();
  return true;
}

void setup() {
  Serial.begin(115200);
  gDio = atoi(SWDIO_STR);
  gClk = atoi(SWCLK_STR);
  gCpuMhz = getCpuFrequencyMhz();
  pinMode(gDio, INPUT);
  pinMode(gClk, INPUT);
}

void loop() {
  if (!Serial.available()) return;
  if (Serial.read() != 'S') return;
  Serial.printf("# EXP E153 v1 git=%s probe=esp32p4_x035 target0=ch32x035f8u6 swdio=%d swclk=%d cpu_mhz=%lu build=%s\n",
                BANNER_GIT, gDio, gClk, (unsigned long)gCpuMhz, __DATE__ " " __TIME__);
  if (!setupBundles()) { Serial.println("ERROR dedic bundle"); return; }
  static const uint32_t kHalfNs[] = {2000, 1000, 500, 300, 200, 150, 125, 100, 75, 50, 25, 0};
  for (bool pp : {false, true})
    for (uint32_t halfNs : kHalfNs) sweepPoint(pp, halfNs);
  releaseBus();
  Serial.println("SWEEP END lines=Hi-Z");
}
