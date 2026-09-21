// E157: 256-byte page program strategies on the dedicated-GPIO RVSWD PHY.
// Destructive to the last 2 KiB of the X035 flash. Plan and report: README.ja.md
#include "../e156_p4_x035_flash_read_strategies/rvswd_dedic.h"

static constexpr uint32_t kFlashKeyr = 0x40022004, kFlashStatr = 0x4002200c, kFlashCtlr = 0x40022010,
                          kFlashAddr = 0x40022014, kFlashModekeyr = 0x40022024;
static constexpr uint32_t kFtpg = 1u << 16, kFter = 1u << 17, kBufload = 1u << 18, kBufrst = 1u << 19, kStrt = 1u << 6;
static constexpr uint32_t kTestBase = 0x0800f000u;
// loader.S assembled with riscv-none-elf-as -march=rv32imac (see README).
static const uint32_t kWriter[] = {0x41044180, 0xc254c004, 0x8b054218, 0x0411ff75, 0x9002c180};

static uint32_t gErrors = 0;

// Scalar store through the program buffer (the current OEP writeWord).
static bool writeWord(uint32_t address, uint32_t value) {
  writeDmi(kAbstractAuto, 0);
  writeDmi(kProgBuf0, 0x0084a023);      // sw s0, 0(s1)
  writeDmi(kProgBuf0 + 1, 0x00100073);  // ebreak
  writeDmi(kData0, address);
  writeDmi(kCommand, 0x00231009);       // s1 <- data0
  if (!waitAbstract()) return false;
  writeDmi(kData0, value);
  writeDmi(kCommand, 0x00271008);       // s0 <- data0, postexec
  return waitAbstract();
}
static bool waitFlash() {
  for (int i = 0; i < 4000; ++i) {
    uint32_t status = 0;
    if (!readWordScalar(kFlashStatr, status)) return false;
    if (!(status & 1)) return (status & 0x10) == 0;  // WRPRTERR
  }
  return false;
}
static bool unlockFlash() {
  uint32_t ctlr = 0;
  gRetries = 0;
  const bool readOk = readWordScalar(kFlashCtlr, ctlr);
  Serial.printf("UNLOCK ctlr_read_ok=%d ctlr=0x%08lx retries=%lu\n", readOk ? 1 : 0, (unsigned long)ctlr, (unsigned long)gRetries);
  if (!readOk) return false;
  if (!(ctlr & 0x8080)) return true;
  const bool k1 = writeWord(kFlashKeyr, 0x45670123), k2 = k1 && writeWord(kFlashKeyr, 0xcdef89ab);
  const bool m1 = k2 && writeWord(kFlashModekeyr, 0x45670123), m2 = m1 && writeWord(kFlashModekeyr, 0xcdef89ab);
  uint32_t after = 0;
  readWordScalar(kFlashCtlr, after);
  Serial.printf("UNLOCK keyr=%d/%d modekeyr=%d/%d ctlr_after=0x%08lx retries=%lu\n", k1, k2, m1, m2, (unsigned long)after, (unsigned long)gRetries);
  return m2 && !(after & 0x8080);
}
static bool erasePage(uint32_t page) {
  return writeWord(kFlashCtlr, kFter) && writeWord(kFlashAddr, page) &&
         writeWord(kFlashCtlr, kFter | kStrt) && waitFlash() && writeWord(kFlashCtlr, 0);
}
static bool finishPage(uint32_t page) {
  return writeWord(kFlashAddr, page) && writeWord(kFlashCtlr, kFtpg | kStrt) && waitFlash() &&
         writeWord(kFlashCtlr, 0);
}
static uint32_t pattern(uint32_t seed, uint32_t page, uint32_t offset) {
  return (seed * 0x9e3779b9u) ^ (page * 0x85ebca6bu) ^ (offset * 0xc2b2ae35u) ^ 0x5a5a1234u;
}
static bool programWordDmi(uint32_t page, uint32_t seed) {
  if (!writeWord(kFlashCtlr, kFtpg) || !writeWord(kFlashCtlr, kFtpg | kBufrst) || !waitFlash()) return false;
  for (uint32_t off = 0; off < 256; off += 4) {
    if (!writeWord(page + off, pattern(seed, page, off))) return false;
    if (!writeWord(kFlashCtlr, kFtpg | kBufload) || !waitFlash()) return false;
  }
  return finishPage(page);
}
static bool programAutoexec(uint32_t page, uint32_t seed) {
  if (!writeWord(kFlashCtlr, kFtpg) || !writeWord(kFlashCtlr, kFtpg | kBufrst) || !waitFlash()) return false;
  const Reply info = readDmiOk(kDmHartInfo);
  if (!info.parity_ok) return false;
  const uint32_t data0 = 0xe0000000u | (info.data & 0x7ff);
  writeDmi(kAbstractAuto, 0);
  writeDmi(kData0, data0);          writeDmi(kCommand, 0x0023100a); if (!waitAbstract()) return false;  // a0
  writeDmi(kData0, data0 + 4);      writeDmi(kCommand, 0x0023100b); if (!waitAbstract()) return false;  // a1
  writeDmi(kData0, kFlashStatr);    writeDmi(kCommand, 0x0023100c); if (!waitAbstract()) return false;  // a2
  writeDmi(kData0, kFtpg | kBufload); writeDmi(kCommand, 0x0023100d); if (!waitAbstract()) return false;  // a3
  for (size_t i = 0; i < sizeof kWriter / sizeof kWriter[0]; ++i) writeDmi(kProgBuf0 + i, kWriter[i]);
  writeDmi(kData1, page);
  // autoexec re-runs whatever is in `command`, so run the writer once explicitly
  // for word 0 (this also loads the progbuf-exec command), then stream the rest.
  writeDmi(kData0, pattern(seed, page, 0));
  writeDmi(kCommand, 0x00240000);   // postexec only: run the writer for word 0
  if (!waitAbstract()) return false;
  writeDmi(kAbstractAuto, 1);       // autoexec on DMDATA0 access
  for (uint32_t off = 4; off < 256; off += 4) writeDmi(kData0, pattern(seed, page, off));
  const bool ok = waitAbstract();
  writeDmi(kAbstractAuto, 0);
  return ok && finishPage(page);
}
static uint32_t verifyPage(uint32_t page, uint32_t seed) {
  uint32_t mismatch = 0;
  if (!prepareSequentialReader(page)) return 64;
  for (uint32_t off = 0; off < 256; off += 4) {
    const Reply r = readDmiOk(kData0);
    if (!r.parity_ok || r.data != pattern(seed, page, off)) ++mismatch;
  }
  waitAbstract();
  writeDmi(kAbstractAuto, 0);
  return mismatch;
}
static void runStrategy(const char *name, bool autoexec, uint32_t seed) {
  uint32_t progUs[8], mismatchTotal = 0;
  for (int i = 0; i < 8; ++i) {
    const uint32_t page = kTestBase + i * 256u;
    gDmiCount = 0; gRetries = 0;
    const int64_t t0 = esp_timer_get_time();
    const bool erased = erasePage(page);
    const int64_t t1 = esp_timer_get_time();
    const bool programmed = erased && (autoexec ? programAutoexec(page, seed) : programWordDmi(page, seed));
    const int64_t t2 = esp_timer_get_time();
    const uint32_t mismatch = verifyPage(page, seed) + (programmed ? 0 : 64);
    const int64_t t3 = esp_timer_get_time();
    progUs[i] = (uint32_t)(t2 - t1); mismatchTotal += mismatch;
    Serial.printf("PAGE strategy=%s page=0x%08lx erase_us=%lld program_us=%lld verify_us=%lld mismatch=%lu dmi=%lu retries=%lu ok=%d\n",
                  name, (unsigned long)page, (long long)(t1 - t0), (long long)(t2 - t1), (long long)(t3 - t2),
                  (unsigned long)mismatch, (unsigned long)gDmiCount, (unsigned long)gRetries, programmed ? 1 : 0);
  }
  for (int i = 1; i < 8; ++i) for (int j = i; j > 0 && progUs[j - 1] > progUs[j]; --j) { const uint32_t t = progUs[j]; progUs[j] = progUs[j - 1]; progUs[j - 1] = t; }
  Serial.printf("SUMMARY strategy=%s pages=8 program_us_min=%lu program_us_median=%lu program_us_max=%lu mismatch_total=%lu\n",
                name, (unsigned long)progUs[0], (unsigned long)progUs[4], (unsigned long)progUs[7], (unsigned long)mismatchTotal);
}

void setup() {
  Serial.begin(115200);
  gDio = atoi(SWDIO_STR); gClk = atoi(SWCLK_STR);
  pinMode(gDio, INPUT); pinMode(gClk, INPUT);
}

void loop() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n'); line.trim();
  if (line.length() < 2 || line[0] != 'P') return;
  const uint32_t seed = line.substring(1).toInt();
  Serial.printf("# EXP E157 v1 git=%s probe=esp32p4_x035 target0=ch32x035f8u6 swdio=%d swclk=%d seed=%lu build=%s\n",
                BANNER_GIT, gDio, gClk, (unsigned long)seed, __DATE__ " " __TIME__);
  if (!setupBundles()) { Serial.println("ERROR dedic bundle"); return; }
  if (!attachAndHalt()) { Serial.println("ERROR halt"); releaseBus(); return; }
  // Half-period margin check with the hart halted. E156 saw two runs with ~60 %
  // parity failures at half 0 ns and three clean runs; pick the first clean half.
  {
    const uint32_t mhz = getCpuFrequencyMhz();
    uint32_t chosen = 0; bool found = false;
    for (uint32_t halfNs : {0u, 25u, 50u, 100u, 200u}) {
      gHalfCycles = (uint32_t)((uint64_t)halfNs * mhz / 1000);
      uint32_t bad = 0, good = 0, badSample[3] = {0, 0, 0}, goodValue = 0; int nb = 0;
      for (int i = 0; i < 10000; ++i) {
        const Reply r = readDmi(kDmStatus);
        if (r.parity_ok) { ++good; goodValue = r.data; } else { ++bad; if (nb < 3) badSample[nb++] = r.data; }
      }
      Serial.printf("SETTLE half_ns=%lu reads=10000 parity_bad=%lu good_value=0x%08lx bad_samples=0x%08lx,0x%08lx,0x%08lx\n",
                    (unsigned long)halfNs, (unsigned long)bad, (unsigned long)goodValue,
                    (unsigned long)badSample[0], (unsigned long)badSample[1], (unsigned long)badSample[2]);
      if (!found && bad == 0) { found = true; chosen = halfNs; }
    }
    gHalfCycles = (uint32_t)((uint64_t)chosen * mhz / 1000);
    Serial.printf("SELECTED half_ns=%lu found=%d\n", (unsigned long)chosen, found ? 1 : 0);
  }
  if (!unlockFlash()) { Serial.println("ERROR unlock"); resumeAndDetach(); return; }
  Serial.println("HALTED unlocked");
  runStrategy("word_dmi", false, seed * 2 + 1);
  runStrategy("autoexec_writer", true, seed * 2 + 2);
  writeWord(kFlashCtlr, 0);
  Serial.printf("RESUME ok=%d lines=Hi-Z\n", resumeAndDetach() ? 1 : 0);
  Serial.println("MEASURE END");
}
