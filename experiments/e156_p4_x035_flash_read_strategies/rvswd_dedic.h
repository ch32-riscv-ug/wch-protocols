// E156 RVSWD PHY: dedicated GPIO, push-pull SWDIO with explicit turnaround
// (E153 result). Kept in a header so the .ino prototype generator sees types first.
#pragma once
#include <Arduino.h>
#include <driver/dedic_gpio.h>
#include <driver/gpio.h>
#include <esp_cpu.h>
#include <esp_timer.h>
#include <hal/dedic_gpio_cpu_ll.h>
#include <hal/gpio_ll.h>
#include <soc/gpio_struct.h>

struct Reply { uint32_t data; bool parity_ok; };

static int gDio = 2, gClk = 54;
static uint32_t gHalfCycles = 0;
static uint32_t gDmiCount = 0;
static uint32_t gRetries = 0;
static dedic_gpio_bundle_handle_t gOut = nullptr;  // bit0 = DIO, bit1 = CLK
static dedic_gpio_bundle_handle_t gIn = nullptr;

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
  if (yes) gpio_ll_output_enable(&GPIO, gDio); else gpio_ll_output_disable(&GPIO, gDio);
}
static void releaseBus() { gpio_ll_output_disable(&GPIO, gDio); gpio_ll_output_disable(&GPIO, gClk); }
static bool setupBundles() {
  if (gOut && gIn) return true;
  pinMode(gDio, OUTPUT | PULLUP);
  pinMode(gClk, OUTPUT);
  const int outPins[] = {gDio, gClk};
  dedic_gpio_bundle_config_t outCfg = {};
  outCfg.gpio_array = outPins; outCfg.array_size = 2; outCfg.flags.out_en = 1;
  if (dedic_gpio_new_bundle(&outCfg, &gOut) != ESP_OK) return false;
  const int inPins[] = {gDio};
  dedic_gpio_bundle_config_t inCfg = {};
  inCfg.gpio_array = inPins; inCfg.array_size = 1; inCfg.flags.in_en = 1;
  if (dedic_gpio_new_bundle(&inCfg, &gIn) != ESP_OK) return false;
  gpio_ll_od_disable(&GPIO, gDio);
  gpio_ll_pullup_en(&GPIO, gDio);
  gpio_ll_input_enable(&GPIO, gDio);
  gpio_set_drive_capability(gpio_num_t(gDio), GPIO_DRIVE_CAP_0);
  gpio_set_drive_capability(gpio_num_t(gClk), GPIO_DRIVE_CAP_0);
  dedic_gpio_cpu_ll_write_mask(0x3, 0x3);
  releaseBus();
  return true;
}
static void configureBus() {
  dedic_gpio_cpu_ll_write_mask(0x3, 0x3);
  gpio_ll_output_enable(&GPIO, gClk);
  gpio_ll_output_enable(&GPIO, gDio);
  delayMicroseconds(20);
  for (int i = 0; i < 100; ++i) { clkLowDio(true); spin(); clkHigh(); spin(); }
  clkLowDio(false); spin(); clkHigh(); spin(); dio(true);
  delayMicroseconds(20);
}
static inline void clockBit(bool v) { clkLowDio(v); spin(); clkHigh(); spin(); }
static inline bool readBit() { clk(false); spin(); const bool v = dioRead(); clkHigh(); spin(); return v; }
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
  ++gDmiCount;
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
  ++gDmiCount;
}

// A DMI read while the DM is busy returns garbage (parity fails). WCH-LinkE
// retries in firmware; do the same, bounded, and count it.
static Reply readDmiOk(uint8_t address) {
  for (int i = 0; i < 200; ++i) {
    const Reply r = readDmi(address);
    if (r.parity_ok) return r;
    ++gRetries;
  }
  Reply bad = {0, false};
  return bad;
}

// ---- RISC-V Debug Module (X035) ----
static constexpr uint8_t kData0 = 0x04, kData1 = 0x05, kDmControl = 0x10, kDmStatus = 0x11,
                         kDmHartInfo = 0x12, kAbstractCs = 0x16, kCommand = 0x17,
                         kAbstractAuto = 0x18, kProgBuf0 = 0x20;

static bool waitAbstract(uint32_t *cmderr = nullptr) {
  for (int i = 0; i < 1000; ++i) {
    const Reply r = readDmiOk(kAbstractCs);
    if (!r.parity_ok) return false;
    if (!(r.data & (1u << 12))) {
      const uint32_t err = (r.data >> 8) & 7;
      if (cmderr) *cmderr = err;
      if (err) { writeDmi(kAbstractCs, 0x700); return false; }
      return true;
    }
  }
  return false;
}
static bool attachAndHalt() {
  configureBus();
  writeDmi(kDmControl, 1);
  writeDmi(kDmControl, 0x80000001);
  for (int i = 0; i < 100; ++i) {
    const Reply r = readDmiOk(kDmStatus);
    if (r.parity_ok && (r.data & (1u << 9))) { writeDmi(kAbstractCs, 0x700); return true; }
  }
  return false;
}
static bool resumeAndDetach() {
  writeDmi(kAbstractAuto, 0);
  writeDmi(kDmControl, 0x40000001);
  bool ok = false;
  for (int i = 0; i < 100; ++i) {
    const Reply r = readDmiOk(kDmStatus);
    if (r.parity_ok && (r.data & (1u << 17))) { ok = true; break; }  // allresumeack
  }
  writeDmi(kDmControl, 0);
  releaseBus();
  return ok;
}
// Scalar word read: exactly the OEP prototype sequence.
static bool readWordScalar(uint32_t address, uint32_t &value) {
  writeDmi(kAbstractAuto, 0);
  writeDmi(kProgBuf0, 0x0004a403);      // lw s0, 0(s1)
  writeDmi(kProgBuf0 + 1, 0x00100073);  // ebreak
  writeDmi(kData0, address);
  writeDmi(kCommand, 0x00231009);       // write s1 <- data0
  if (!waitAbstract()) return false;
  writeDmi(kCommand, 0x00241000);       // exec progbuf
  if (!waitAbstract()) return false;
  writeDmi(kCommand, 0x00221008);       // read s0 -> data0
  if (!waitAbstract()) return false;
  const Reply r = readDmiOk(kData0);
  value = r.data;
  return r.parity_ok;
}
static bool prepareSequentialReader(uint32_t first_address) {
  const Reply info = readDmiOk(kDmHartInfo);
  if (!info.parity_ok) return false;
  const uint32_t data0_address = 0xe0000000u | (info.data & 0x7ff);
  writeDmi(kAbstractAuto, 0);
  writeDmi(kData0, data0_address);      writeDmi(kCommand, 0x0023100a);  // a0 = &DATA0
  if (!waitAbstract()) return false;
  writeDmi(kData0, data0_address + 4);  writeDmi(kCommand, 0x0023100b);  // a1 = &DATA1
  if (!waitAbstract()) return false;
  writeDmi(kProgBuf0, 0x40044180);      // c.lw s0,0(a1); c.lw s1,0(s0)
  writeDmi(kProgBuf0 + 1, 0xc1040411);  // c.addi s0,4; c.sw s1,0(a0)
  writeDmi(kProgBuf0 + 2, 0x9002c180);  // c.sw s0,0(a1); c.ebreak
  writeDmi(kData1, first_address);
  writeDmi(kAbstractAuto, 1);           // autoexec on DATA0 access
  writeDmi(kCommand, 0x00240000);       // first run
  return true;
}
