// E159: does the DMCONTROL write order decide whether the X035 hart parks at the
// reset vector after ndmreset? Plan and report: README.ja.md. Flash is not written.
#include "../e156_p4_x035_flash_read_strategies/rvswd_dedic.h"

// The OEP PHY attaches at half 500 ns (init clocks + dmactive) and only then
// switches to the fast half. Doing the re-attach at half 0 here gave 25-44 %
// corrupted samples in the first run (2026-09-22). Same rule for every attach.
static uint32_t gFastHalfCycles = 0;
static void attachSlow() {
  const uint32_t fast = gHalfCycles;
  gHalfCycles = (uint32_t)(500ull * getCpuFrequencyMhz() / 1000);
  configureBus();
  writeDmi(kDmControl, 1);
  gHalfCycles = fast;
}
// Every attach re-selects the half period like RvswdPhy::attach(): 1000 clean,
// consistent DMSTATUS reads at the smallest half. Run 2 (one selection per run)
// had 43 % parity failures. Returns the chosen half in ns, or 0xffffffff.
static const uint32_t kHalfNs[] = {0, 25, 50, 100, 200, 500};
static uint32_t gHalfHist[7] = {0};
static uint32_t attachSelect() {
  attachSlow();
  const uint32_t mhz = getCpuFrequencyMhz();
  for (size_t h = 0; h < sizeof kHalfNs / sizeof kHalfNs[0]; ++h) {
    gHalfCycles = (uint32_t)((uint64_t)kHalfNs[h] * mhz / 1000);
    uint32_t first = 0; bool clean = true;
    for (int i = 0; i < 1000 && clean; ++i) {
      const Reply r = readDmi(kDmStatus);
      if (!r.parity_ok) { clean = false; break; }
      if (i == 0) first = r.data; else if (r.data != first) clean = false;
    }
    if (clean && ((first >> 8) & 0xf) != 0) { ++gHalfHist[h]; return kHalfNs[h]; }
  }
  ++gHalfHist[6];
  return 0xffffffffu;
}
static bool haltNow() {
  writeDmi(kDmControl, 0x80000001);
  for (int i = 0; i < 100; ++i) {
    const Reply r = readDmiOk(kDmStatus);
    if (r.parity_ok && (r.data & (1u << 9))) { writeDmi(kAbstractCs, 0x700); return true; }
  }
  return false;
}
static bool readCsr(uint16_t regno, uint32_t &value) {
  writeDmi(kAbstractAuto, 0);
  writeDmi(kCommand, 0x00220000u | regno);
  if (!waitAbstract()) return false;
  const Reply r = readDmiOk(kData0);
  value = r.data;
  return r.parity_ok;
}
static bool waitStatusBit(int bit, bool set, int polls = 200) {
  for (int i = 0; i < polls; ++i) {
    const Reply r = readDmiOk(kDmStatus);
    if (r.parity_ok && (((r.data >> bit) & 1) == (set ? 1u : 0u))) return true;
    delayMicroseconds(200);
  }
  return false;
}
// Write "ndmreset released" until DMCONTROL reads back with bits 1:0 == 01 (Ch32Dm::resetOnce).
static bool releaseNdmreset(uint32_t keep) {
  for (int i = 0; i < 50; ++i) {
    writeDmi(kDmControl, keep | 0x1);
    const Reply r = readDmiOk(kDmControl);
    if (r.parity_ok && (r.data & 0x3) == 0x1) return true;
    delay(1);
  }
  return false;
}
static void waitRunningResumeIfHalted() {
  for (int polls = 0; polls < 200; ++polls) {
    const Reply r = readDmiOk(kDmStatus);
    if (!r.parity_ok) { delayMicroseconds(500); continue; }
    const bool unavail = r.data & (1u << 13), halted = r.data & (1u << 9), allrunning = r.data & (1u << 11);
    if (unavail) { delayMicroseconds(500); continue; }
    if (halted) { writeDmi(kDmControl, 0x40000001); delayMicroseconds(500); continue; }
    if (allrunning) return;
    delayMicroseconds(500);
  }
}
static void reattachOnce() {
  attachSelect();
  for (int i = 0; i < 100; ++i) readDmi(kDmStatus);
  writeDmi(kDmControl, 0);
  releaseBus();
  delay(2);
}

// All variants start attached with the hart halted (haltreq asserted), except `running`.
static bool seqBaseline(bool dmactiveCycle, bool reattach) {
  writeDmi(kAbstractAuto, 0);
  writeDmi(kDmControl, 0x00000003); delay(1);
  const bool released = step(0, releaseNdmreset(0));
  if (dmactiveCycle) { writeDmi(kDmControl, 0); delayMicroseconds(200); writeDmi(kDmControl, 1); delay(1); }
  waitRunningResumeIfHalted();
  writeDmi(kDmControl, 0x10000001); writeDmi(kDmControl, 0x00000001); delay(1);
  writeDmi(kDmControl, 0); releaseBus(); delay(2);
  if (reattach) reattachOnce();
  return released;
}
static bool seqPlain() {
  writeDmi(kAbstractAuto, 0);
  writeDmi(kDmControl, 0x00000003); delay(1);
  const bool released = step(0, releaseNdmreset(0));
  writeDmi(kDmControl, 0); releaseBus(); delay(2);
  return released;
}
static bool seqHaltreqThrough() {
  writeDmi(kAbstractAuto, 0);
  writeDmi(kDmControl, 0x80000003); delay(1);
  const bool released = step(0, releaseNdmreset(0x80000000));
  const bool halted = step(1, waitStatusBit(9, true));          // allhalted out of reset
  writeDmi(kDmControl, 0x90000001);                    // ackhavereset, haltreq kept
  writeDmi(kDmControl, 0x40000001);                    // resumereq
  const bool resumed = step(2, waitStatusBit(17, true));        // allresumeack
  writeDmi(kDmControl, 0x00000001);
  writeDmi(kDmControl, 0); releaseBus(); delay(2);
  return released && halted && resumed;
}
static bool seqResethaltreq() {
  writeDmi(kAbstractAuto, 0);
  writeDmi(kDmControl, 0x00000009);                    // setresethaltreq
  writeDmi(kDmControl, 0x0000000b); delay(1);          // + ndmreset
  const bool released = step(0, releaseNdmreset(0x8));
  const bool halted = step(1, waitStatusBit(9, true));
  writeDmi(kDmControl, 0x00000005);                    // clrresethaltreq
  writeDmi(kDmControl, 0x10000001);                    // ackhavereset
  writeDmi(kDmControl, 0x40000001);
  const bool resumed = step(2, waitStatusBit(17, true));
  writeDmi(kDmControl, 0x00000001);
  writeDmi(kDmControl, 0); releaseBus(); delay(2);
  return released && halted && resumed;
}
static bool seqRunning() {
  writeDmi(kAbstractAuto, 0);
  writeDmi(kDmControl, 0x40000001);                    // let it run first
  waitStatusBit(17, true);
  writeDmi(kDmControl, 0x00000001); delay(5);
  writeDmi(kDmControl, 0x00000003); delay(1);
  const bool released = step(0, releaseNdmreset(0));
  writeDmi(kDmControl, 0); releaseBus(); delay(2);
  return released;
}

struct Variant { const char *name; int parked, haltFail, sampleFail, seqFail; };
static int gStep[7][3] = {{0}};
static int gCur = 0;
static bool step(int which, bool ok) { if (!ok) ++gStep[gCur][which]; return ok; }
static Variant gVariants[] = {{"baseline", 0, 0, 0, 0}, {"no_reattach", 0, 0, 0, 0}, {"no_dmactive_cycle", 0, 0, 0, 0},
                              {"plain", 0, 0, 0, 0}, {"haltreq_through", 0, 0, 0, 0}, {"resethaltreq", 0, 0, 0, 0},
                              {"running", 0, 0, 0, 0}};
static constexpr int kVariants = sizeof gVariants / sizeof gVariants[0];

static bool runSequence(int v) {
  switch (v) {
    case 0: return seqBaseline(true, true);
    case 1: return seqBaseline(true, false);
    case 2: return seqBaseline(false, true);
    case 3: return seqPlain();
    case 4: return seqHaltreqThrough();
    case 5: return seqResethaltreq();
    default: return seqRunning();
  }
}

void setup() {
  Serial.begin(115200);
  gDio = atoi(SWDIO_STR); gClk = atoi(SWCLK_STR);
  pinMode(gDio, INPUT); pinMode(gClk, INPUT);
}

void loop() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n'); line.trim();
  if (line.length() < 1 || line[0] != 'P') return;
  const int cycles = line.length() > 1 ? line.substring(1).toInt() : 100;
  Serial.printf("# EXP E159 v1 git=%s probe=esp32p4_x035 target0=ch32x035f8u6 swdio=%d swclk=%d cycles=%d build=%s\n",
                BANNER_GIT, gDio, gClk, cycles, __DATE__ " " __TIME__);
  if (!setupBundles()) { Serial.println("ERROR dedic bundle"); return; }
  gHalfCycles = 0;
  const uint32_t half0 = attachSelect();
  if (!haltNow()) { Serial.println("ERROR halt"); releaseBus(); return; }
  Serial.printf("SELECTED half_ns=%lu found=%d\n", (unsigned long)half0, half0 != 0xffffffffu ? 1 : 0);
  for (Variant &v : gVariants) v.parked = v.haltFail = v.sampleFail = v.seqFail = 0;
  for (auto &row : gStep) row[0] = row[1] = row[2] = 0;
  for (auto &h : gHalfHist) h = 0;
  // Round-robin so drift affects every variant alike. The hart is halted (haltreq held) at the top of each cycle.
  for (int i = 0; i < cycles; ++i) {
    for (int v = 0; v < kVariants; ++v) {
      gCur = v;
      if (!runSequence(v)) ++gVariants[v].seqFail;
      delay(20);
      const uint32_t half = attachSelect();
      const Reply st = readDmiOk(kDmStatus);
      const uint32_t retriesBefore = gRetries;
      if (!haltNow()) { ++gVariants[v].haltFail; Serial.printf("CYCLE variant=%s i=%d halt_fail=1 half_ns=%lu dmstatus=0x%08lx\n", gVariants[v].name, i, (unsigned long)half, (unsigned long)st.data); continue; }
      uint32_t dpc = 0, mtvec = 0;
      const bool ok = readCsr(0x7b1, dpc) && readCsr(0x305, mtvec);
      if (!ok) ++gVariants[v].sampleFail;
      const bool parked = ok && dpc == 0 && mtvec == 0;
      if (parked) ++gVariants[v].parked;
      if (parked || !ok)
        Serial.printf("CYCLE variant=%s i=%d parked=%d dpc=0x%08lx mtvec=0x%08lx dmstatus=0x%08lx sample_ok=%d retries=%lu half_ns=%lu\n",
                      gVariants[v].name, i, parked ? 1 : 0, (unsigned long)dpc, (unsigned long)mtvec, (unsigned long)st.data, ok ? 1 : 0,
                      (unsigned long)(gRetries - retriesBefore), (unsigned long)half);
      // stay halted (haltreq held) for the next variant's sequence; `running` resumes itself
    }
    if ((i + 1) % 25 == 0) Serial.printf("PROGRESS i=%d\n", i + 1);
  }
  Serial.printf("RETRIES total=%lu dmi=%lu\n", (unsigned long)gRetries, (unsigned long)gDmiCount);
  Serial.printf("HALF_HIST 0=%lu 25=%lu 50=%lu 100=%lu 200=%lu 500=%lu none=%lu\n", (unsigned long)gHalfHist[0], (unsigned long)gHalfHist[1],
                (unsigned long)gHalfHist[2], (unsigned long)gHalfHist[3], (unsigned long)gHalfHist[4], (unsigned long)gHalfHist[5], (unsigned long)gHalfHist[6]);
  for (int v = 0; v < kVariants; ++v)
    Serial.printf("SUMMARY variant=%s cycles=%d parked=%d halt_fail=%d sample_fail=%d seq_fail=%d release_fail=%d halted_fail=%d resume_fail=%d\n",
                  gVariants[v].name, cycles, gVariants[v].parked, gVariants[v].haltFail, gVariants[v].sampleFail, gVariants[v].seqFail,
                  gStep[v][0], gStep[v][1], gStep[v][2]);
  releaseBus(); delay(5);
  attachSelect();
  const bool finalHalt = haltNow();
  Serial.printf("RESUME ok=%d\n", (finalHalt && resumeAndDetach()) ? 1 : 0);
  Serial.println("MEASURE END");
}
