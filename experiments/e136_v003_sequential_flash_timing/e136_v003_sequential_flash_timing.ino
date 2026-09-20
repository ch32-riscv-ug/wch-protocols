#define E129_EMBEDDED
#include "../e129_swio_only_cpu_boot/e129_swio_only_cpu_boot.ino"

static constexpr int kMeasureCoefficient = 8;
static constexpr uint32_t kMeasureAddress = 0x08003fc0u;

struct WaitResult {
  bool ok;
  uint32_t elapsedUs;
  uint16_t polls;
  uint16_t readErrors;
  uint32_t status;
};

static WaitResult measuredWaitIdle() {
  WaitResult result = {};
  const uint32_t started = micros();
  for (uint16_t poll = 1; poll <= 400; ++poll) {
    uint32_t status = 0;
    if (readMemoryWord(0x4002200cu, &status, kMeasureCoefficient)) {
      ++result.readErrors;
      continue;
    }
    result.status = status;
    if (!(status & 1u)) {
      result.ok = true;
      result.polls = poll;
      break;
    }
  }
  result.elapsedUs = micros() - started;
  // readMemoryWord leaves the reader program installed.
  if (result.ok) result.ok = !prepareWordWriter(kMeasureCoefficient);
  return result;
}

static void printWait(const char* stage, uint16_t quietUs, int index,
                      const WaitResult& result) {
  Serial.printf("TIMING quiet_us=%u stage=%s index=%d ok=%u elapsed_us=%lu polls=%u read_errors=%u STATR=0x%08lx\n",
                quietUs, stage, index, result.ok, (unsigned long)result.elapsedUs,
                result.polls, result.readErrors, (unsigned long)result.status);
}

static bool sequentialPage(uint16_t quietUs, uint8_t seed) {
  if (!attachHaltWriter(kMeasureCoefficient)) return false;
  const struct { uint32_t address; uint32_t value; } unlock[] = {
    {0x40022004u, 0x45670123u}, {0x40022004u, 0xcdef89abu},
    {0x40022024u, 0x45670123u}, {0x40022024u, 0xcdef89abu},
  };
  for (const auto& item : unlock) {
    if (!flashWriteWord(item.address, item.value, kMeasureCoefficient)) return false;
  }

  auto write = [quietUs](uint32_t address, uint32_t value) {
    const bool ok = flashWriteWord(address, value, kMeasureCoefficient);
    if (quietUs) delayMicroseconds(quietUs);
    return ok;
  };

  if (!write(0x40022010u, 0x00020000u) ||
      !write(0x40022014u, kMeasureAddress) ||
      !write(0x40022010u, 0x00020040u)) return false;
  WaitResult wait = measuredWaitIdle();
  printWait("erase", quietUs, -1, wait);
  if (!wait.ok) return false;

  if (!write(0x40022010u, 0x00010000u) ||
      !write(0x40022010u, 0x00090000u)) return false;
  wait = measuredWaitIdle();
  printWait("buffer_reset", quietUs, -1, wait);
  if (!wait.ok) return false;

  for (int index = 0; index < 16; ++index) {
    uint32_t word = 0;
    for (int byte = 0; byte < 4; ++byte) {
      const uint8_t value = seed == 0xff ? 0xff :
          static_cast<uint8_t>(seed + (index * 4 + byte) * 37);
      word |= static_cast<uint32_t>(value) << (byte * 8);
    }
    if (!write(kMeasureAddress + index * 4u, word) ||
        !write(0x40022010u, 0x00050000u)) return false;
    wait = measuredWaitIdle();
    printWait("buffer_load", quietUs, index, wait);
    if (!wait.ok) return false;
  }

  if (!write(0x40022014u, kMeasureAddress) ||
      !write(0x40022010u, 0x00010040u)) return false;
  wait = measuredWaitIdle();
  printWait("program", quietUs, -1, wait);
  if (!wait.ok || !write(0x40022010u, 0)) return false;

  for (int index = 0; index < 16; ++index) {
    uint32_t expected = 0;
    for (int byte = 0; byte < 4; ++byte) {
      const uint8_t value = seed == 0xff ? 0xff :
          static_cast<uint8_t>(seed + (index * 4 + byte) * 37);
      expected |= static_cast<uint32_t>(value) << (byte * 8);
    }
    uint32_t actual = 0;
    if (readMemoryWord(kMeasureAddress + index * 4u, &actual,
                       kMeasureCoefficient) || actual != expected) {
      Serial.printf("VERIFY quiet_us=%u seed=%u index=%d expected=0x%08lx actual=0x%08lx\n",
                    quietUs, seed, index, (unsigned long)expected,
                    (unsigned long)actual);
      return false;
    }
  }
  return true;
}

static void measureAll() {
  const uint16_t quiet[] = {0, 50, 200, 1000};
  for (size_t index = 0; index < sizeof(quiet) / sizeof(quiet[0]); ++index) {
    const uint32_t started = millis();
    const bool pattern = sequentialPage(quiet[index], 0x31 + index * 19);
    const bool restore = sequentialPage(quiet[index], 0xff);
    Serial.printf("CASE quiet_us=%u pattern=%u restore=%u elapsed_ms=%lu\n",
                  quiet[index], pattern, restore,
                  (unsigned long)(millis() - started));
    if (!pattern || !restore) break;
  }
  Serial.println("MEASURE END");
}

void setup() { e129_setup(); }

void loop() {
  if (!Serial.available()) return;
  const int command = Serial.read();
  if (command == '?') {
    Serial.println("# EXP E136 v003-sequential-flash-timing");
    Serial.println("READY commands=M");
  } else if (command == 'M') {
    measureAll();
  }
}
