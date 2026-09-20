#define setup e142_setup
#define loop e142_loop
#include "../e142_p4_x035_rvswd_pair/e142_p4_x035_rvswd_pair.ino"
#undef setup
#undef loop

// E143 reuses E142's verified RVSWD PHY, then accesses X035 registers through
// RISC-V abstract commands. Target USB PC16/17 and debug PC18/19 are excluded.

static constexpr uint8_t DMDATA0 = 0x04;
static constexpr uint8_t DMCONTROL = 0x10;
static constexpr uint8_t DMSTATUS = 0x11;
static constexpr uint8_t ABSTRACTCS = 0x16;
static constexpr uint8_t COMMAND = 0x17;
static constexpr uint8_t ABSTRACTAUTO = 0x18;
static constexpr uint8_t PROGBUF0 = 0x20;
static constexpr uint8_t PROGBUF1 = 0x21;

static uint32_t dmiRead(uint8_t reg, bool *ok = nullptr) {
  Reply r = readDmi(reg);
  if (ok) *ok = r.parity_ok;
  return r.data;
}

static void dmiWrite(uint8_t reg, uint32_t value) { (void)writeDmi(reg, value); }

static bool waitAbstract() {
  for (int i = 0; i < 100; ++i) {
    bool ok;
    uint32_t v = dmiRead(ABSTRACTCS, &ok);
    if (!ok) return false;
    if (!(v & (1u << 12))) {
      if ((v >> 8) & 7) {
        dmiWrite(ABSTRACTCS, 0x00000700);
        return false;
      }
      return true;
    }
  }
  return false;
}

static bool haltTarget() {
  initializeBus();
  dmiWrite(DMCONTROL, 0x00000001);
  dmiWrite(DMCONTROL, 0x80000001);
  for (int i = 0; i < 100; ++i) {
    bool ok;
    uint32_t s = dmiRead(DMSTATUS, &ok);
    if (ok && (s & (1u << 9))) return true;  // anyhalted
  }
  return false;
}

static bool readWord(uint32_t address, uint32_t *value) {
  dmiWrite(ABSTRACTAUTO, 0);
  dmiWrite(PROGBUF0, 0x0004a403);  // lw x8, 0(x9)
  dmiWrite(PROGBUF1, 0x00100073);  // ebreak
  dmiWrite(DMDATA0, address);
  dmiWrite(COMMAND, 0x00231009);   // x9 = DATA0
  if (!waitAbstract()) return false;
  dmiWrite(COMMAND, 0x00241000);   // execute progbuf
  if (!waitAbstract()) return false;
  dmiWrite(COMMAND, 0x00221008);   // DATA0 = x8
  if (!waitAbstract()) return false;
  bool ok;
  *value = dmiRead(DMDATA0, &ok);
  return ok;
}

static bool writeWord(uint32_t address, uint32_t value) {
  dmiWrite(ABSTRACTAUTO, 0);
  dmiWrite(PROGBUF0, 0x0084a023);  // sw x8, 0(x9)
  dmiWrite(PROGBUF1, 0x00100073);  // ebreak
  dmiWrite(DMDATA0, address);
  dmiWrite(COMMAND, 0x00231009);   // x9 = DATA0
  if (!waitAbstract()) return false;
  dmiWrite(DMDATA0, value);
  dmiWrite(COMMAND, 0x00271008);   // x8 = DATA0, execute progbuf
  return waitAbstract();
}

static uint64_t sampleP4() {
  uint64_t bits = 0;
  for (uint8_t pin : kCandidates) {
    if (pin == 2 || pin == 54) continue;
    pinMode(pin, INPUT);
    if (digitalRead(pin)) bits |= 1ULL << pin;
  }
  return bits;
}

static void printChanged(uint8_t port, uint8_t bit, uint64_t low, uint64_t high,
                         uint64_t low2, uint64_t high2) {
  uint64_t changed = (low ^ high) & ~(low ^ low2) & ~(high ^ high2);
  changed &= ~(1ULL << 2);
  changed &= ~(1ULL << 54);
  if (!changed) return;
  Serial.printf("MAP target=P%c%u low=0x%08lx%08lx high=0x%08lx%08lx p4=",
                'A' + port, bit,
                (unsigned long)(low >> 32), (unsigned long)low,
                (unsigned long)(high >> 32), (unsigned long)high);
  bool first = true;
  for (uint8_t pin : kCandidates) {
    if (changed & (1ULL << pin)) {
      if (!first) Serial.print(',');
      Serial.print(pin);
      first = false;
    }
  }
  Serial.println();
}

static bool excludedTarget(uint8_t port, uint8_t bit) {
  return port == 2 && bit >= 16 && bit <= 19;
}

static bool readInputs(uint32_t values[4]) {
  for (uint8_t port = 0; port < 4; ++port) {
    if (!readWord(0x40010808 + uint32_t(port) * 0x400, &values[port])) return false;
  }
  return true;
}

static void printReverse(uint8_t p4, const uint32_t low[4], const uint32_t high[4]) {
  bool any = false;
  for (uint8_t port = 0; port < 4; ++port) {
    uint32_t changed = (low[port] ^ high[port]) & 0x00ffffff;
    if (port == 2) changed &= ~0x000f0000u;
    for (uint8_t bit = 0; bit < 24; ++bit) {
      if (!(changed & (1u << bit))) continue;
      if (!any) Serial.printf("REVERSE p4=%u target=", p4);
      else Serial.print(',');
      Serial.printf("P%c%u", 'A' + port, bit);
      any = true;
    }
  }
  if (any) Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("# EXP E143 v1 P4-X035 pin map");
  gDio = 2;
  gClk = 54;
  gHalfUs = 1;
  releaseAll();
  if (!haltTarget()) {
    Serial.println("FATAL halt_failed");
    return;
  }
  Serial.printf("HALT dmstatus=0x%08lx\n", (unsigned long)dmiRead(DMSTATUS));

  uint32_t rcc = 0;
  if (!readWord(0x40021018, &rcc)) {
    Serial.println("FATAL read_primitive_failed");
    return;
  }
  Serial.printf("READ RCC_APB2PCENR=0x%08lx\n", (unsigned long)rcc);
  if (!writeWord(0x40021018, rcc | 0x1c)) {
    Serial.println("FATAL write_primitive_failed");
    return;
  }

  int maps = 0;
  for (uint8_t port = 0; port < 4; ++port) {
    const uint32_t base = 0x40010800 + uint32_t(port) * 0x400;
    for (uint8_t bit = 0; bit < 24; ++bit) {
      if (excludedTarget(port, bit)) continue;
      const uint32_t cfgAddr = base + (bit < 8 ? 0x00 : bit < 16 ? 0x04 : 0x1c);
      const uint8_t shift = (bit & 7) * 4;
      uint32_t cfg = 0, out = 0;
      if (!readWord(cfgAddr, &cfg) || !readWord(base + 0x0c, &out)) continue;
      uint32_t testCfg = (cfg & ~(0xfu << shift)) | (0x1u << shift);
      if (!writeWord(base + 0x0c, out & ~(1u << bit)) ||
          !writeWord(cfgAddr, testCfg)) continue;
      delayMicroseconds(100);
      uint64_t low = sampleP4();
      if (!writeWord(base + 0x0c, out | (1u << bit))) continue;
      delayMicroseconds(100);
      uint64_t high = sampleP4();
      writeWord(base + 0x0c, out & ~(1u << bit));
      delayMicroseconds(100);
      uint64_t low2 = sampleP4();
      writeWord(base + 0x0c, out | (1u << bit));
      delayMicroseconds(100);
      uint64_t high2 = sampleP4();
      uint64_t changed = (low ^ high) & ~(low ^ low2) & ~(high ^ high2) &
                         ~(1ULL << 2) & ~(1ULL << 54);
      if (changed) {
        ++maps;
        printChanged(port, bit, low, high, low2, high2);
      }
      writeWord(cfgAddr, cfg);
      writeWord(base + 0x0c, out);
    }
  }

  // Independent reverse-direction cross-check. Preserve all twelve config
  // words, make non-USB/non-debug target pins floating inputs, then toggle one
  // P4 line at a time and read all four target INDR registers.
  uint32_t savedCfg[4][3] = {};
  for (uint8_t port = 0; port < 4; ++port) {
    const uint32_t base = 0x40010800 + uint32_t(port) * 0x400;
    readWord(base + 0x00, &savedCfg[port][0]);
    readWord(base + 0x04, &savedCfg[port][1]);
    readWord(base + 0x1c, &savedCfg[port][2]);
    writeWord(base + 0x00, 0x44444444);
    writeWord(base + 0x04, 0x44444444);
    uint32_t upper = 0x44444444;
    if (port == 2) upper = savedCfg[port][2];  // leave PC16-23 untouched
    writeWord(base + 0x1c, upper);
  }
  int reverseMaps = 0;
  for (uint8_t pin : kCandidates) {
    if (pin == 2 || pin == 54) continue;
    pinMode(pin, OUTPUT);
    gpio_set_drive_capability(gpio_num_t(pin), GPIO_DRIVE_CAP_0);
    digitalWrite(pin, LOW);
    delayMicroseconds(100);
    uint32_t low[4] = {}, high[4] = {};
    if (!readInputs(low)) continue;
    digitalWrite(pin, HIGH);
    delayMicroseconds(100);
    if (!readInputs(high)) continue;
    bool any = false;
    for (uint8_t port = 0; port < 4; ++port) {
      uint32_t changed = (low[port] ^ high[port]) & 0x00ffffff;
      if (port == 2) changed &= ~0x000f0000u;
      any |= changed != 0;
    }
    if (any) {
      ++reverseMaps;
      printReverse(pin, low, high);
    }
    pinMode(pin, INPUT);
  }
  for (uint8_t port = 0; port < 4; ++port) {
    const uint32_t base = 0x40010800 + uint32_t(port) * 0x400;
    writeWord(base + 0x00, savedCfg[port][0]);
    writeWord(base + 0x04, savedCfg[port][1]);
    writeWord(base + 0x1c, savedCfg[port][2]);
  }
  writeWord(0x40021018, rcc);
  dmiWrite(DMCONTROL, 0x40000001);  // ndmreset
  dmiWrite(DMCONTROL, 0x00000001);
  dmiWrite(DMCONTROL, 0x00000000);
  releaseAll();
  Serial.printf("DONE maps=%d reverse_maps=%d target_reset=1 lines=Hi-Z\n",
                maps, reverseMaps);
}

void loop() { delay(1000); }
