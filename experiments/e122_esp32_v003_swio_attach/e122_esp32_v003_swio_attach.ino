// E122: attach a classic ESP32 to CH32V003 PD1 using the one-wire SWIO PHY.
//
// This only enables the debug module and reads DMI registers. It never sends a
// halt request, abstract command, memory operation, or flash operation.

#include <Arduino.h>
#include "driver/gpio.h"
#include "soc/gpio_struct.h"

static constexpr gpio_num_t kSwioPin = GPIO_NUM_16;
static constexpr uint32_t kSwioMask = 1u << 16;
static constexpr uint8_t kDmControl = 0x10;
static constexpr uint8_t kDmStatus = 0x11;
static constexpr uint8_t kDmHartInfo = 0x12;
static constexpr uint8_t kDmCfgr = 0x7d;
static constexpr uint8_t kDmShdwCfgr = 0x7e;
static constexpr uint32_t kCfgrValue = 0x5aa50400;
static constexpr int kReadRiseTimeout = 1000;

static portMUX_TYPE gSwioMux = portMUX_INITIALIZER_UNLOCKED;

static inline void IRAM_ATTR preciseDelay(int count) {
  asm volatile(
      "1: addi %[count], %[count], -1\n"
      "   bbci %[count], 31, 1b\n"
      : [count] "+r"(count));
}

static inline void IRAM_ATTR lineLow() { GPIO.out_w1tc = kSwioMask; }
static inline void IRAM_ATTR lineRelease() { GPIO.out_w1ts = kSwioMask; }

static inline void IRAM_ATTR sendOne(int coefficient) {
  lineLow();
  preciseDelay(coefficient);
  lineRelease();
  preciseDelay(coefficient);
}

static inline void IRAM_ATTR sendZero(int coefficient) {
  lineLow();
  preciseDelay(coefficient * 4);
  lineRelease();
  preciseDelay(coefficient);
}

// Return 0/1 for a decoded target bit, or 2 if the target never releases LOW.
static inline int IRAM_ATTR readBit(int coefficient) {
  lineLow();
  preciseDelay(coefficient);
  lineRelease();  // Open-drain release lets the target extend the LOW pulse.
  preciseDelay(coefficient * 2);
  const int sampled = (GPIO.in & kSwioMask) != 0;

  for (int timeout = 0; timeout < kReadRiseTimeout; ++timeout) {
    if (GPIO.in & kSwioMask) {
      preciseDelay(coefficient / 2);
      return sampled;
    }
  }
  return 2;
}

static void IRAM_ATTR writeDmi(uint8_t address, uint32_t value, int coefficient) {
  portENTER_CRITICAL(&gSwioMux);
  sendOne(coefficient);  // start
  for (uint8_t mask = 0x40; mask != 0; mask >>= 1) {
    (address & mask) ? sendOne(coefficient) : sendZero(coefficient);
  }
  sendOne(coefficient);  // write
  for (uint32_t mask = 0x80000000u; mask != 0; mask >>= 1) {
    (value & mask) ? sendOne(coefficient) : sendZero(coefficient);
  }
  portEXIT_CRITICAL(&gSwioMux);
  delayMicroseconds(8);
}

static int IRAM_ATTR readDmi(uint8_t address, uint32_t *value, int coefficient) {
  uint32_t result = 0;
  portENTER_CRITICAL(&gSwioMux);
  sendOne(coefficient);  // start
  for (uint8_t mask = 0x40; mask != 0; mask >>= 1) {
    (address & mask) ? sendOne(coefficient) : sendZero(coefficient);
  }
  sendZero(coefficient);  // read
  for (int bit = 0; bit < 32; ++bit) {
    result <<= 1;
    const int decoded = readBit(coefficient);
    if (decoded == 2) {
      portEXIT_CRITICAL(&gSwioMux);
      return -21;
    }
    result |= decoded;
  }
  portEXIT_CRITICAL(&gSwioMux);
  delayMicroseconds(8);
  *value = result;
  return 0;
}

static void configureReleasedOpenDrain() {
  gpio_config_t config = {};
  config.pin_bit_mask = uint64_t{1} << kSwioPin;
  config.mode = GPIO_MODE_INPUT_OUTPUT_OD;
  config.pull_up_en = GPIO_PULLUP_ENABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&config);
  lineRelease();
}

static void printRegister(const char *name, int status, uint32_t value) {
  Serial.print(name);
  Serial.print(" status=");
  Serial.print(status);
  Serial.print(" value=0x");
  Serial.printf("%08lx\n", static_cast<unsigned long>(value));
}

static void attemptAttach() {
  pinMode(kSwioPin, INPUT_PULLUP);
  delay(2);
  const int idle = digitalRead(kSwioPin);
  Serial.print("IDLE gpio16=");
  Serial.println(idle);
  if (idle == LOW) {
    Serial.println("ATTACH STOP reason=idle_low");
    return;
  }

  configureReleasedOpenDrain();
  const int coefficients[] = {10, 9, 11, 8, 12};
  for (int coefficient : coefficients) {
    writeDmi(kDmShdwCfgr, kCfgrValue, coefficient);
    writeDmi(kDmCfgr, kCfgrValue, coefficient);
    writeDmi(kDmShdwCfgr, kCfgrValue, coefficient);
    writeDmi(kDmCfgr, kCfgrValue, coefficient);
    writeDmi(kDmControl, 1, coefficient);
    writeDmi(kDmControl, 1, coefficient);

    uint32_t cfgr = 0;
    const int status = readDmi(kDmCfgr, &cfgr, coefficient);
    Serial.print("TRY coefficient=");
    Serial.print(coefficient);
    Serial.print(" status=");
    Serial.print(status);
    Serial.print(" DMCFGR=0x");
    Serial.printf("%08lx\n", static_cast<unsigned long>(cfgr));
    if (status != 0 || (cfgr & 0xffff0000u) != 0x5aa50000u) {
      continue;
    }

    uint32_t dmstatus = 0;
    uint32_t hartinfo = 0;
    const int dmstatusResult = readDmi(kDmStatus, &dmstatus, coefficient);
    const int hartinfoResult = readDmi(kDmHartInfo, &hartinfo, coefficient);
    printRegister("DMSTATUS", dmstatusResult, dmstatus);
    printRegister("DMHARTINFO", hartinfoResult, hartinfo);
    Serial.print("ATTACH OK coefficient=");
    Serial.println(coefficient);
    return;
  }
  Serial.println("ATTACH FAIL reason=no_signature");
}

void setup() {
  Serial.begin(115200);
  pinMode(kSwioPin, INPUT_PULLUP);  // Never drive the target during boot.
}

void loop() {
  if (!Serial.available()) {
    return;
  }
  const int command = Serial.read();
  if (command == '?') {
    Serial.println("# EXP E122 classic-esp32 gpio16->ch32v003-pd1 swio");
    Serial.println("READY commands=A");
  } else if (command == 'A') {
    Serial.println("ATTACH BEGIN");
    attemptAttach();
    Serial.println("ATTACH END");
  }
}
