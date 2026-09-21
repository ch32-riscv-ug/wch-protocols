// E156: flash read strategies on the dedicated-GPIO RVSWD PHY. Read-only.
// Plan and report: README.ja.md
#include "rvswd_dedic.h"

static constexpr uint32_t kFlashBase = 0x08000000u;
static constexpr uint32_t kFlashBytes = 63488u;

static uint32_t crc32Update(uint32_t crc, uint32_t word) {
  for (int b = 0; b < 4; ++b) {
    crc ^= (word >> (8 * b)) & 0xff;
    for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return crc;
}

static void runStrategy(const char *name, uint32_t gap_us = 0) {
  gDmiCount = 0; gRetries = 0;
  uint32_t errors = 0, crc = 0xffffffffu, cmderr = 0;
  const int64_t t0 = esp_timer_get_time();
  if (!strcmp(name, "scalar")) {
    for (uint32_t a = kFlashBase; a < kFlashBase + kFlashBytes; a += 4) {
      uint32_t v = 0;
      if (!readWordScalar(a, v)) ++errors;
      crc = crc32Update(crc, v);
    }
  } else {
    const bool poll = !strcmp(name, "autoexec_poll");
    if (!prepareSequentialReader(kFlashBase)) ++errors;
    for (uint32_t a = kFlashBase; a < kFlashBase + kFlashBytes; a += 4) {
      if (poll && !waitAbstract()) ++errors;
      if (gap_us) delayMicroseconds(gap_us);
      const Reply r = readDmiOk(kData0);
      if (!r.parity_ok) ++errors;
      crc = crc32Update(crc, r.data);
    }
    // The last DATA0 read launched one look-ahead past the range; let it finish
    // and record cmderr (the final look-ahead address is still inside flash+4).
    waitAbstract(&cmderr);
    writeDmi(kAbstractAuto, 0);
  }
  const int64_t t1 = esp_timer_get_time();
  Serial.printf("READ strategy=%s gap_us=%lu bytes=%lu us=%lld dmi=%lu retries=%lu errors=%lu cmderr=%lu crc32=0x%08lx\n",
                name, (unsigned long)gap_us, (unsigned long)kFlashBytes, (long long)(t1 - t0), (unsigned long)gDmiCount,
                (unsigned long)gRetries, (unsigned long)errors, (unsigned long)cmderr, (unsigned long)(crc ^ 0xffffffffu));
}

// Diagnostic: with the hart halted (no abstract command running), does the DM
// still answer cleanly at the E153 clock? Returns the smallest all-pass half_ns.
static uint32_t sweepHalted() {
  static const uint32_t kHalfNs[] = {0, 25, 50, 100, 150, 200, 300, 500, 1000};
  uint32_t selected = 1000;
  bool found = false;
  const uint32_t mhz = getCpuFrequencyMhz();
  for (uint32_t halfNs : kHalfNs) {
    gHalfCycles = (uint32_t)((uint64_t)halfNs * mhz / 1000);
    uint32_t okStatus = 0, matchStatus = 0, okCs = 0, matchCs = 0, first = 0, firstCs = 0;
    for (int i = 0; i < 1000; ++i) {
      const Reply r = readDmi(kDmStatus);
      if (i == 0) first = r.data;
      okStatus += r.parity_ok; matchStatus += (r.data == first);
    }
    for (int i = 0; i < 1000; ++i) {
      const Reply r = readDmi(kAbstractCs);
      if (i == 0) firstCs = r.data;
      okCs += r.parity_ok; matchCs += (r.data == firstCs);
    }
    Serial.printf("HALTED_SWEEP half_ns=%lu dmstatus_parity=%lu dmstatus_match=%lu dmstatus=0x%08lx abstractcs_parity=%lu abstractcs_match=%lu abstractcs=0x%08lx\n",
                  (unsigned long)halfNs, (unsigned long)okStatus, (unsigned long)matchStatus, (unsigned long)first,
                  (unsigned long)okCs, (unsigned long)matchCs, (unsigned long)firstCs);
    if (!found && okStatus == 1000 && matchStatus == 1000 && okCs == 1000 && matchCs == 1000) { found = true; selected = halfNs; }
  }
  gHalfCycles = (uint32_t)((uint64_t)selected * mhz / 1000);
  Serial.printf("SELECTED half_ns=%lu found=%d\n", (unsigned long)selected, found ? 1 : 0);
  return selected;
}

static void measureBusy() {
  // One program-buffer run (lw + ebreak) launched by command; poll ABSTRACTCS
  // until busy clears, timing from the command write to the first not-busy read.
  writeDmi(kAbstractAuto, 0);
  writeDmi(kProgBuf0, 0x0004a403);
  writeDmi(kProgBuf0 + 1, 0x00100073);
  writeDmi(kData0, kFlashBase);
  writeDmi(kCommand, 0x00231009);
  waitAbstract();
  uint32_t samples[100];
  for (int i = 0; i < 100; ++i) {
    const int64_t t0 = esp_timer_get_time();
    writeDmi(kCommand, 0x00241000);
    int polls = 0;
    for (;;) { const Reply r = readDmi(kAbstractCs); ++polls; if (r.parity_ok && !(r.data & (1u << 12))) break; if (polls > 100) break; }
    samples[i] = (uint32_t)(esp_timer_get_time() - t0);
    if (i < 3) Serial.printf("BUSY_SAMPLE polls=%d us=%lu\n", polls, (unsigned long)samples[i]);
  }
  for (int i = 1; i < 100; ++i) for (int j = i; j > 0 && samples[j - 1] > samples[j]; --j) { const uint32_t t = samples[j]; samples[j] = samples[j - 1]; samples[j - 1] = t; }
  Serial.printf("BUSY samples=100 min_us=%lu median_us=%lu max_us=%lu note=command_write+first_not_busy_poll\n",
                (unsigned long)samples[0], (unsigned long)samples[50], (unsigned long)samples[99]);
}

void setup() {
  Serial.begin(115200);
  gDio = atoi(SWDIO_STR);
  gClk = atoi(SWCLK_STR);
  pinMode(gDio, INPUT);
  pinMode(gClk, INPUT);
}

void loop() {
  if (!Serial.available()) return;
  if (Serial.read() != 'R') return;
  Serial.printf("# EXP E156 v1 git=%s probe=esp32p4_x035 target0=ch32x035f8u6 swdio=%d swclk=%d cpu_mhz=%lu build=%s\n",
                BANNER_GIT, gDio, gClk, (unsigned long)getCpuFrequencyMhz(), __DATE__ " " __TIME__);
  if (!setupBundles()) { Serial.println("ERROR dedic bundle"); return; }
  if (!attachAndHalt()) { Serial.println("ERROR halt"); releaseBus(); return; }
  Serial.println("HALTED");
  sweepHalted();
  runStrategy("scalar");
  runStrategy("autoexec_poll");
  for (uint32_t gap : {0u, 10u, 20u, 50u, 100u}) runStrategy("autoexec_nopoll", gap);
  measureBusy();
  Serial.printf("RESUME ok=%d lines=Hi-Z\n", resumeAndDetach() ? 1 : 0);
  Serial.println("MEASURE END");
}
