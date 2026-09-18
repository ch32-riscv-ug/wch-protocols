#include <Arduino.h>
#include "driver/gpio.h"
#include "soc/gpio_struct.h"

static constexpr gpio_num_t kPin = GPIO_NUM_16;
static constexpr uint32_t kMask = 1u << 16;
static constexpr uint8_t DMCONTROL = 0x10;
static constexpr uint8_t DMSTATUS = 0x11;
static constexpr uint8_t DMHARTINFO = 0x12;
static constexpr uint8_t DMCFGR = 0x7d;
static constexpr uint8_t DMSHDWCFGR = 0x7e;
static constexpr uint32_t kCfgr = 0x5aa50400;
static portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;

static inline void IRAM_ATTR waitCycles(int n) {
  asm volatile("1: addi %[n], %[n], -1\n   bbci %[n], 31, 1b\n" : [n] "+r"(n));
}
static inline void IRAM_ATTR low() { GPIO.out_w1tc = kMask; }
static inline void IRAM_ATTR high() { GPIO.out_w1ts = kMask; }
static inline void IRAM_ATTR outputOn() { GPIO.enable_w1ts = kMask; }
static inline void IRAM_ATTR outputOff() { GPIO.enable_w1tc = kMask; }

static inline void IRAM_ATTR send1(int c) {
  low(); waitCycles(c); high(); waitCycles(c);
}
static inline void IRAM_ATTR send0(int c) {
  low(); waitCycles(c * 4); high(); waitCycles(c);
}

// This is the reference implementation's R_GLITCH_HIGH read sequence.
static inline int IRAM_ATTR readBit(int c) {
  const int medium = c * 2;
  low();
  waitCycles(c);
  outputOff();
  high();
  waitCycles(c / 2);
  outputOn();                 // briefly recharge the line HIGH
  outputOff();                // then let the target own it again
  waitCycles(c / 2);
  const int sampled = (GPIO.in & kMask) != 0;
  if (!sampled) {
    waitCycles(medium);
    outputOn();
    outputOff();
  }
  for (int timeout = 0; timeout < 1000; ++timeout) {
    if (GPIO.in & kMask) {
      outputOn();
      waitCycles(c / 2);
      return sampled;
    }
  }
  outputOn();
  return 2;
}

static void IRAM_ATTR writeDmi(uint8_t address, uint32_t value, int c) {
  high(); outputOn();
  portENTER_CRITICAL(&gMux);
  send1(c);
  for (uint8_t mask = 0x40; mask; mask >>= 1) (address & mask) ? send1(c) : send0(c);
  send1(c);
  for (uint32_t mask = 0x80000000u; mask; mask >>= 1) (value & mask) ? send1(c) : send0(c);
  portEXIT_CRITICAL(&gMux);
  delayMicroseconds(8);
}

static int IRAM_ATTR readDmi(uint8_t address, uint32_t *value, int c) {
  high(); outputOn();
  uint32_t result = 0;
  portENTER_CRITICAL(&gMux);
  send1(c);
  for (uint8_t mask = 0x40; mask; mask >>= 1) (address & mask) ? send1(c) : send0(c);
  send0(c);
  for (int bit = 0; bit < 32; ++bit) {
    result <<= 1;
    const int decoded = readBit(c);
    if (decoded == 2) {
      portEXIT_CRITICAL(&gMux);
      return -21;
    }
    result |= decoded;
  }
  portEXIT_CRITICAL(&gMux);
  delayMicroseconds(8);
  *value = result;
  return 0;
}

static void configureReferenceIo() {
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = uint64_t{1} << kPin;
  cfg.mode = GPIO_MODE_INPUT_OUTPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&cfg);
  high(); outputOn();
}

static void printReg(const char *name, int status, uint32_t value) {
  Serial.printf("%s status=%d value=0x%08lx\n", name, status,
                static_cast<unsigned long>(value));
}

static void attach() {
  pinMode(kPin, INPUT_PULLUP);
  delay(2);
  const int idle = digitalRead(kPin);
  Serial.printf("IDLE gpio16=%d\n", idle);
  if (!idle) {
    Serial.println("ATTACH STOP reason=idle_low");
    return;
  }
  configureReferenceIo();
  const int coefficients[] = {10, 9, 11, 8, 12};
  for (int c : coefficients) {
    writeDmi(DMSHDWCFGR, kCfgr, c);
    writeDmi(DMCFGR, kCfgr, c);
    writeDmi(DMSHDWCFGR, kCfgr, c);
    writeDmi(DMCFGR, kCfgr, c);
    writeDmi(DMCONTROL, 1, c);
    writeDmi(DMCONTROL, 1, c);
    uint32_t cfgr = 0;
    const int status = readDmi(DMCFGR, &cfgr, c);
    Serial.printf("TRY coefficient=%d status=%d DMCFGR=0x%08lx\n", c, status,
                  static_cast<unsigned long>(cfgr));
    if (status || (cfgr & 0xffff0000u) != 0x5aa50000u) continue;
    uint32_t dmstatus = 0, hartinfo = 0;
    const int sr = readDmi(DMSTATUS, &dmstatus, c);
    const int hr = readDmi(DMHARTINFO, &hartinfo, c);
    printReg("DMSTATUS", sr, dmstatus);
    printReg("DMHARTINFO", hr, hartinfo);
    Serial.printf("ATTACH OK coefficient=%d\n", c);
    return;
  }
  Serial.println("ATTACH FAIL reason=no_signature");
}

void setup() {
  Serial.begin(115200);
  pinMode(kPin, INPUT_PULLUP);
}

void loop() {
  if (!Serial.available()) return;
  const int command = Serial.read();
  if (command == '?') {
    Serial.println("# EXP E123 classic-esp32 swio-high-recharge");
    Serial.println("READY commands=A");
  } else if (command == 'A') {
    Serial.println("ATTACH BEGIN");
    attach();
    Serial.println("ATTACH END");
  }
}
