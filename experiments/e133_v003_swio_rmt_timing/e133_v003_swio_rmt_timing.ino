#include <Arduino.h>
#include "esp32-hal-rmt.h"
#include "driver/gpio.h"
#include "soc/gpio_struct.h"

namespace {
constexpr uint8_t kPin = 16;
constexpr uint32_t kMask = 1u << kPin;
constexpr uint8_t kDmCfgr = 0x7d;
constexpr uint8_t kDmShadowCfgr = 0x7e;
constexpr uint8_t kData1 = 0x05;
constexpr uint32_t kCfgr = 0x5aa50400;
constexpr uint32_t kRmtHz = 80000000;
constexpr size_t kCaptureSymbols = 256;
rmt_data_t capture[kCaptureSymbols];
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

inline void IRAM_ATTR waitCycles(int count) {
  asm volatile("1: addi %[n], %[n], -1\n   bbci %[n], 31, 1b\n"
               : [n] "+r"(count));
}
inline void IRAM_ATTR low() { GPIO.out_w1tc = kMask; }
inline void IRAM_ATTR high() { GPIO.out_w1ts = kMask; }
inline void IRAM_ATTR outputOn() { GPIO.enable_w1ts = kMask; }
inline void IRAM_ATTR outputOff() { GPIO.enable_w1tc = kMask; }
inline void IRAM_ATTR sendOne(int c) { low(); waitCycles(c); high(); waitCycles(c); }
inline void IRAM_ATTR sendZero(int c) { low(); waitCycles(c * 4); high(); waitCycles(c); }

inline int IRAM_ATTR readBit(int c) {
  low(); waitCycles(c); outputOff(); high();
  waitCycles(c / 2); outputOn(); outputOff(); waitCycles(c / 2);
  const int sampled = (GPIO.in & kMask) != 0;
  if (!sampled) { waitCycles(c * 2); outputOn(); outputOff(); }
  for (int timeout = 0; timeout < 1000; ++timeout) {
    if (GPIO.in & kMask) { outputOn(); waitCycles(c / 2); return sampled; }
  }
  outputOn(); return 2;
}

void IRAM_ATTR writeDmi(uint8_t address, uint32_t value, int c) {
  high(); outputOn(); portENTER_CRITICAL(&mux); sendOne(c);
  for (uint8_t mask = 0x40; mask; mask >>= 1)
    (address & mask) ? sendOne(c) : sendZero(c);
  sendOne(c);
  for (uint32_t mask = 0x80000000u; mask; mask >>= 1)
    (value & mask) ? sendOne(c) : sendZero(c);
  portEXIT_CRITICAL(&mux); delayMicroseconds(8);
}

int IRAM_ATTR readDmiRaw(uint8_t address, uint32_t* value, int c) {
  high(); outputOn(); uint32_t result = 0; portENTER_CRITICAL(&mux); sendOne(c);
  for (uint8_t mask = 0x40; mask; mask >>= 1)
    (address & mask) ? sendOne(c) : sendZero(c);
  sendZero(c);
  for (int bit = 0; bit < 32; ++bit) {
    result <<= 1; const int decoded = readBit(c);
    if (decoded == 2) { portEXIT_CRITICAL(&mux); return -21; }
    result |= decoded;
  }
  portEXIT_CRITICAL(&mux); delayMicroseconds(8); *value = result; return 0;
}

void configureIo() {
  gpio_config_t config = {};
  config.pin_bit_mask = uint64_t{1} << kPin;
  config.mode = GPIO_MODE_INPUT_OUTPUT;
  config.pull_up_en = GPIO_PULLUP_ENABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&config); high(); outputOn();
}

void printCapture(int coefficient, size_t count) {
  Serial.printf("CAPTURE coefficient=%d symbols=%u tick_ps=12500", coefficient,
                static_cast<unsigned>(count));
  for (size_t i = 0; i < count; ++i) {
    Serial.printf(" %c%u,%c%u", capture[i].level0 ? 'H' : 'L',
                  capture[i].duration0, capture[i].level1 ? 'H' : 'L',
                  capture[i].duration1);
  }
  Serial.println();
}

void runCapture(int coefficient) {
  writeDmi(kDmShadowCfgr, kCfgr, coefficient);
  writeDmi(kDmCfgr, kCfgr, coefficient);
  size_t count = kCaptureSymbols;
  memset(capture, 0, sizeof(capture));
  if (!rmtReadAsync(kPin, capture, &count)) {
    Serial.printf("RESULT coefficient=%d arm=failed\n", coefficient); return;
  }
  uint32_t captured = 0;
  const int capturedStatus = readDmiRaw(kDmCfgr, &captured, coefficient);
  const uint32_t deadline = millis() + 100;
  while (!rmtReceiveCompleted(kPin) && static_cast<int32_t>(deadline - millis()) > 0) {}
  uint32_t plain = 0;
  const int plainStatus = readDmiRaw(kDmCfgr, &plain, coefficient);
  Serial.printf("RESULT coefficient=%d captured_status=%d captured=0x%08lx plain_status=%d plain=0x%08lx\n",
                coefficient, capturedStatus, static_cast<unsigned long>(captured),
                plainStatus, static_cast<unsigned long>(plain));
  printCapture(coefficient, count);
}

void runWriteSurvey(int coefficient) {
  int verifiedBy8 = 0;
  int verifiedBySame = 0;
  for (uint32_t trial = 0; trial < 100; ++trial) {
    const uint32_t expected = 0x08000000u ^ (trial * 0x01020409u);
    if (trial == 0) {
      size_t count = kCaptureSymbols;
      memset(capture, 0, sizeof(capture));
      rmtReadAsync(kPin, capture, &count);
      writeDmi(kData1, expected, coefficient);
      const uint32_t deadline = millis() + 100;
      while (!rmtReceiveCompleted(kPin) &&
             static_cast<int32_t>(deadline - millis()) > 0) {}
      Serial.printf("WRITE_CAPTURE coefficient=%d ", coefficient);
      printCapture(coefficient, count);
    } else {
      writeDmi(kData1, expected, coefficient);
    }
    uint32_t actual = 0;
    if (!readDmiRaw(kData1, &actual, 8) && actual == expected) ++verifiedBy8;
    actual = 0;
    if (!readDmiRaw(kData1, &actual, coefficient) && actual == expected)
      ++verifiedBySame;
  }
  Serial.printf("WRITE_RESULT coefficient=%d verify_c8=%d/100 verify_same=%d/100\n",
                coefficient, verifiedBy8, verifiedBySame);
}
}  // namespace

void setup() {
  Serial.begin(115200); configureIo();
  if (!rmtInit(kPin, RMT_RX_MODE, RMT_MEM_NUM_BLOCKS_4, kRmtHz)) {
    Serial.println("RMT INIT FAILED"); return;
  }
  rmtSetRxMinThreshold(kPin, 0);
  rmtSetRxMaxThreshold(kPin, 1600);
  Serial.println("# EXP E133 v003-swio-rmt-timing");
  Serial.println("READY commands=CW");
}

void loop() {
  if (!Serial.available()) return;
  const int command = Serial.read();
  if (command == 'C') {
    for (int coefficient = 8; coefficient <= 14; ++coefficient)
      runCapture(coefficient);
    Serial.println("CAPTURE END");
  } else if (command == 'W') {
    writeDmi(kDmShadowCfgr, kCfgr, 8);
    writeDmi(kDmCfgr, kCfgr, 8);
    for (int coefficient = 8; coefficient <= 14; ++coefficient)
      runWriteSurvey(coefficient);
    Serial.println("WRITE END");
  }
}
