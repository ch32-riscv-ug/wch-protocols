#define E126_EMBEDDED
#include "../e126_swio_ndmreset_boot_hid/e126_swio_ndmreset_boot_hid.ino"

static constexpr int kTargetResetPin = 23;

static void hardwareResetTarget() {
  pinMode(kTargetResetPin, OUTPUT);
  digitalWrite(kTargetResetPin, LOW);
  delay(20);
  pinMode(kTargetResetPin, INPUT);
}

static int readThenRestoreWriter(uint32_t address, uint32_t *value, int c) {
  int result = readMemoryWord(address, value, c);
  if (result) return result;
  return prepareWordWriter(c);
}

static bool setPd4OutputLow(int c) {
  // RCC APB2PCENR |= RCC_IOPDEN (bit 5).
  uint32_t apb2pcenr = 0;
  int result = readThenRestoreWriter(0x40021018, &apb2pcenr, c);
  if (result) return false;
  result = writeMemoryWordRetry(0x40021018, apb2pcenr | 0x20, c);
  if (result) return false;

  // PD4 is CFGLR nibble 4. Arduino OUTPUT is 50 MHz push-pull = 0b0011.
  uint32_t cfglr = 0;
  result = readThenRestoreWriter(0x40011400, &cfglr, c);
  if (result) return false;
  const uint32_t configured = (cfglr & ~(0xfu << 16)) | (0x3u << 16);
  result = writeMemoryWordRetry(0x40011400, configured, c);
  if (result) return false;

  // Explicitly drive PD4 LOW (GPIO BCR offset 0x14), matching reset default.
  result = writeMemoryWordRetry(0x40011414, 1u << 4, c);
  if (result) return false;

  uint32_t clockRead = 0, configRead = 0, outRead = 0;
  result = readThenRestoreWriter(0x40021018, &clockRead, c);
  if (result) return false;
  result = readThenRestoreWriter(0x40011400, &configRead, c);
  if (result) return false;
  result = readThenRestoreWriter(0x4001140c, &outRead, c);
  if (result) return false;
  Serial.printf("PD4 READBACK APB2PCENR=0x%08lx CFGLR=0x%08lx OUTDR=0x%08lx\n",
                (unsigned long)clockRead, (unsigned long)configRead,
                (unsigned long)outRead);
  return (clockRead & 0x20u) && ((configRead >> 16) & 0xfu) == 3u &&
         (outRead & (1u << 4)) == 0;
}

static void seamlessBoot() {
  const int c = 10;
  if (!attachHaltWriter(c)) { Serial.println("SEAMLESS STOP reason=prepare_failed"); return; }

  uint32_t before = 0;
  int result = readThenRestoreWriter(0x4002200c, &before, c);
  Serial.printf("SEAMLESS STATR_BEFORE status=%d value=0x%08lx\n", result,
                (unsigned long)before);
  if (result) { Serial.println("SEAMLESS STOP reason=statr_read_failed"); return; }

  result = writeMemoryWordRetry(0x40022028, 0x45670123, c);
  if (!result) result = writeMemoryWordRetry(0x40022028, 0xcdef89ab, c);
  if (!result) result = writeMemoryWordRetry(0x4002200c, 0x00004000, c);
  Serial.printf("SEAMLESS BOOT_MODE_WRITE status=%d\n", result);
  if (result) { Serial.println("SEAMLESS STOP reason=boot_mode_write_failed"); return; }

  uint32_t statr = 0;
  result = readThenRestoreWriter(0x4002200c, &statr, c);
  Serial.printf("SEAMLESS STATR_AFTER status=%d value=0x%08lx\n", result,
                (unsigned long)statr);
  if (result || !(statr & 0x4000u)) {
    Serial.println("SEAMLESS STOP reason=boot_mode_readback_failed"); return;
  }

  if (!setPd4OutputLow(c)) {
    Serial.println("SEAMLESS STOP reason=pd4_failed"); return;
  }
  Serial.println("SEAMLESS USB_DETACH_BEGIN ms=100"); Serial.flush();
  delay(100);
  Serial.println("SEAMLESS HWRESET ASSERT pin=23"); Serial.flush();
  hardwareResetTarget();
  Serial.println("SEAMLESS HWRESET RELEASE pin=23");
}

#ifndef E127_EMBEDDED
void setup() {
  pinMode(kTargetResetPin, INPUT);
  e123_setup();
}
void loop() {
  if (!Serial.available()) return;
  const int command = Serial.read();
  if (command == '?') {
    Serial.println("# EXP E127 uiap-seamless-boot-swio");
    Serial.println("READY commands=BR");
  } else if (command == 'B') {
    Serial.println("SEAMLESS BEGIN"); seamlessBoot(); Serial.println("SEAMLESS END");
  } else if (command == 'R') {
    Serial.println("RESET ASSERT pin=23"); Serial.flush();
    hardwareResetTarget();
    Serial.println("RESET RELEASE pin=23");
  }
}
#endif
