// Reuse the PHY proven by E123, but provide a separate experiment entry point.
#define setup e123_setup
#define loop e123_loop
#include "../e123_esp32_v003_swio_recharge/e123_esp32_v003_swio_recharge.ino"
#undef setup
#undef loop

static constexpr uint8_t kDmAbstractCs = 0x16;
static constexpr uint8_t kDmCommand = 0x17;
static constexpr uint8_t kDmAbstractAuto = 0x18;
static constexpr uint8_t kData0 = 0x04;
static constexpr uint8_t kData1 = 0x05;
static constexpr uint8_t kProgBuf0 = 0x20;

static int waitAbstract(int c) {
  for (int timeout = 0; timeout < 1000; ++timeout) {
    uint32_t value = 0;
    const int status = readDmi(kDmAbstractCs, &value, c);
    if (status) return status;
    if ((value & (1u << 12)) == 0) {
      const int commandError = (value >> 8) & 7;
      if (commandError) {
        writeDmi(kDmAbstractCs, 0x00000700, c);
        return -30 - commandError;
      }
      return 0;
    }
  }
  return -8;
}

static int haltTarget(int c, uint32_t *lastStatus) {
  writeDmi(DMCONTROL, 0x80000001, c);
  writeDmi(DMCONTROL, 0x80000001, c);
  for (int timeout = 0; timeout < 100; ++timeout) {
    const int status = readDmi(DMSTATUS, lastStatus, c);
    if (status) return status;
    if ((*lastStatus & 0x00000300u) == 0x00000300u) return 0;
    delay(1);
  }
  return -9;
}

static int prepareWordWriter(int c) {
  uint32_t hartInfo = 0;
  int result = readDmi(DMHARTINFO, &hartInfo, c);
  if (result) return result;
  const uint32_t data0Address = 0xe0000000u | (hartInfo & 0x7ffu);

  writeDmi(kDmAbstractAuto, 0, c);
  writeDmi(kData0, data0Address, c);
  writeDmi(kDmCommand, 0x0023100a, c);       // x10 = DATA0 address
  result = waitAbstract(c);
  if (result) return result;
  writeDmi(kData0, data0Address + 4, c);
  writeDmi(kDmCommand, 0x0023100b, c);       // x11 = DATA1 address
  result = waitAbstract(c);
  if (result) return result;

  // c.lw x8,0(x10); c.lw x9,0(x11); c.sw x8,0(x9);
  // c.addi x9,4; c.sw x9,0(x11); c.ebreak
  writeDmi(kProgBuf0 + 0, 0x41844100, c);
  writeDmi(kProgBuf0 + 1, 0x0491c080, c);
  writeDmi(kProgBuf0 + 2, 0x9002c184, c);
  return 0;
}

static int writeMemoryWord(uint32_t address, uint32_t value, int c, bool wait) {
  writeDmi(kData1, address, c);
  writeDmi(kData0, value, c);
  writeDmi(kDmCommand, 0x00240000, c);       // execute program buffer
  return wait ? waitAbstract(c) : 0;
}

static void bootHid() {
  pinMode(kPin, INPUT_PULLUP);
  delay(2);
  const int idle = digitalRead(kPin);
  Serial.printf("IDLE gpio16=%d\n", idle);
  if (!idle) {
    Serial.println("BOOT STOP reason=idle_low");
    return;
  }

  const int c = 10;
  configureReferenceIo();
  writeDmi(DMSHDWCFGR, kCfgr, c);
  writeDmi(DMCFGR, kCfgr, c);
  writeDmi(DMSHDWCFGR, kCfgr, c);
  writeDmi(DMCFGR, kCfgr, c);
  writeDmi(DMCONTROL, 1, c);
  writeDmi(DMCONTROL, 1, c);
  uint32_t cfgr = 0;
  int result = readDmi(DMCFGR, &cfgr, c);
  Serial.printf("ATTACH status=%d DMCFGR=0x%08lx\n", result,
                static_cast<unsigned long>(cfgr));
  if (result || (cfgr & 0xffff0000u) != 0x5aa50000u) {
    Serial.println("BOOT STOP reason=attach_failed");
    return;
  }

  uint32_t dmstatus = 0;
  result = haltTarget(c, &dmstatus);
  Serial.printf("HALT status=%d DMSTATUS=0x%08lx\n", result,
                static_cast<unsigned long>(dmstatus));
  if (result) {
    Serial.println("BOOT STOP reason=halt_failed");
    return;
  }

  result = prepareWordWriter(c);
  Serial.printf("WRITER status=%d\n", result);
  if (result) {
    Serial.println("BOOT STOP reason=writer_failed");
    return;
  }

  const struct { uint32_t address; uint32_t value; const char *name; } writes[] = {
    {0x40022028, 0x45670123, "BOOT_KEY1"},
    {0x40022028, 0xcdef89ab, "BOOT_KEY2"},
    {0x4002200c, 0x00004000, "BOOT_MODE"},
    {0x40021024, 0x01000000, "RESET_FLAGS_CLEAR"},
  };
  for (const auto &item : writes) {
    result = writeMemoryWord(item.address, item.value, c, true);
    Serial.printf("WRITE %s address=0x%08lx value=0x%08lx status=%d\n",
                  item.name, static_cast<unsigned long>(item.address),
                  static_cast<unsigned long>(item.value), result);
    if (result) {
      Serial.println("BOOT STOP reason=register_write_failed");
      return;
    }
  }

  Serial.println("RESET REQUEST address=0xe000e048 value=0xbeef0080");
  Serial.flush();
  writeMemoryWord(0xe000e048, 0xbeef0080, c, false);
  delay(20);
  Serial.println("RESET SENT");
}

void setup() { e123_setup(); }

void loop() {
  if (!Serial.available()) return;
  const int command = Serial.read();
  if (command == '?') {
    Serial.println("# EXP E124 swio-boot-hid-reenumerate");
    Serial.println("READY commands=B");
  } else if (command == 'B') {
    Serial.println("BOOT BEGIN");
    bootHid();
    Serial.println("BOOT END");
  }
}
