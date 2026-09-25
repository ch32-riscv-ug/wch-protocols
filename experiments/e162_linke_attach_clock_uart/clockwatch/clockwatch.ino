// clockwatch: the target reports its own clock registers, independent of the debug probe.
//
// - Every 100 ms it prints "CW <seq> CTLR=... CFGR0=... ACTLR=... CFGR2=..." on Serial (USART1, 115200 baud
//   computed from the clock set at boot). If a debugger re-clocks the chip, the baud drifts and the text garbles:
//   that is the visible consequence for an application.
// - Whenever any of those registers changes it appends an entry to cw_log[] in RAM (seq, CTLR, CFGR0, ACTLR,
//   CFGR2), so the values the running code saw before and after an attach can be read back later.
#include <Arduino.h>

#define REG(a) (*(volatile uint32_t *)(a))
#define RCC_CTLR  0x40021000u
#define RCC_CFGR0 0x40021004u
#define RCC_CFGR2 0x4002102Cu
#define FLASH_ACTLR 0x40022000u

struct Entry { uint32_t seq, ctlr, cfgr0, actlr, cfgr2; };
extern "C" {
volatile uint32_t cw_magic = 0;          // 0xC10C4A7C once running
volatile uint32_t cw_count = 0;          // entries written (wraps modulo CW_N)
volatile uint32_t cw_seq = 0;            // loop counter
volatile Entry cw_log[32];
}
#define CW_N 32

static uint32_t rd_cfgr2() {
#if defined(CH32V20x) || defined(CH32V30x) || defined(CH32L10x)
  return REG(RCC_CFGR2);
#else
  return 0;
#endif
}

static Entry last;

static void snapshot(bool force) {
  Entry e = {cw_seq, REG(RCC_CTLR), REG(RCC_CFGR0), REG(FLASH_ACTLR), rd_cfgr2()};
  if (force || e.ctlr != last.ctlr || e.cfgr0 != last.cfgr0 || e.actlr != last.actlr || e.cfgr2 != last.cfgr2) {
    uint32_t i = cw_count % CW_N;
    cw_log[i].seq = e.seq; cw_log[i].ctlr = e.ctlr; cw_log[i].cfgr0 = e.cfgr0;
    cw_log[i].actlr = e.actlr; cw_log[i].cfgr2 = e.cfgr2;
    cw_count = cw_count + 1;
    last = e;
  }
}

void setup() {
  Serial.begin(115200);
#ifdef CW_AHB_DIV2
  // Run at HCLK = F_CPU / 2 (HPRE = /2) so that a debugger resetting HPRE to /1 is visible; USART1 is on APB2
  // (PPRE2 = /1), so its BRR follows HCLK.
  REG(RCC_CFGR0) = (REG(RCC_CFGR0) & ~0xF0u) | 0x80u;
  REG(0x40013808u) = (uint16_t)((F_CPU / 2 + 115200 / 2) / 115200);
#endif
  snapshot(true);
  cw_magic = 0xC10C4A7Cu;
}

void loop() {
  static uint32_t t0 = millis();
  ++cw_seq;
  snapshot(false);
  if (millis() - t0 >= 100) {
    t0 = millis();
    Serial.print("CW "); Serial.print(cw_seq);
    Serial.print(" CTLR="); Serial.print(last.ctlr, HEX);
    Serial.print(" CFGR0="); Serial.print(last.cfgr0, HEX);
    Serial.print(" ACTLR="); Serial.print(last.actlr, HEX);
    Serial.print(" CFGR2="); Serial.print(last.cfgr2, HEX);
    Serial.print(" n="); Serial.println(cw_count);
  }
}
