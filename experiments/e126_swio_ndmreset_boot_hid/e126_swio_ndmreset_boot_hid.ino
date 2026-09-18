#define E125_EMBEDDED
#include "../e125_swio_user_to_boot_hid/e125_swio_user_to_boot_hid.ino"

static int readMemoryWord(uint32_t address, uint32_t *value, int c) {
  writeDmi(kDmAbstractAuto, 0, c);
  writeDmi(kProgBuf0 + 0, 0x40044180, c);
  writeDmi(kProgBuf0 + 1, 0xc1040001, c);
  writeDmi(kProgBuf0 + 2, 0x9002c180, c);
  writeDmi(kData1, address, c);
  writeDmi(kDmCommand, 0x00240000, c);
  int result = waitAbstract(c);
  if (result) return result;
  return readDmi(kData0, value, c);
}

static int writeMemoryWordRetry(uint32_t address, uint32_t value, int c) {
  int result = 0;
  for (int attempt = 1; attempt <= 3; ++attempt) {
    result = writeMemoryWord(address, value, c, true);
    Serial.printf("MODE WORD attempt=%d address=0x%08lx status=%d\n",
                  attempt, (unsigned long)address, result);
    if (result == 0) return 0;
    result = prepareWordWriter(c);
    if (result) return result;
  }
  return result;
}

static bool attachHaltWriter(int c) {
  pinMode(kPin, INPUT_PULLUP);
  delay(2);
  if (!digitalRead(kPin)) { Serial.println("MODE STOP reason=idle_low"); return false; }
  configureReferenceIo();
  writeDmi(DMSHDWCFGR, kCfgr, c); writeDmi(DMCFGR, kCfgr, c);
  writeDmi(DMSHDWCFGR, kCfgr, c); writeDmi(DMCFGR, kCfgr, c);
  writeDmi(DMCONTROL, 1, c); writeDmi(DMCONTROL, 1, c);
  uint32_t cfgr = 0;
  int result = readDmi(DMCFGR, &cfgr, c);
  Serial.printf("MODE ATTACH status=%d DMCFGR=0x%08lx\n", result, (unsigned long)cfgr);
  if (result || (cfgr & 0xffff0000u) != 0x5aa50000u) return false;
  uint32_t status = 0;
  result = haltTarget(c, &status);
  Serial.printf("MODE HALT status=%d DMSTATUS=0x%08lx\n", result, (unsigned long)status);
  if (result) return false;
  result = prepareWordWriter(c);
  Serial.printf("MODE WRITER status=%d\n", result);
  return result == 0;
}

static void switchMode(bool boot) {
  const int c = 10;
  if (!attachHaltWriter(c)) { Serial.println("MODE STOP reason=prepare_failed"); return; }
  const struct { uint32_t value; const char *name; } writes[] = {
    {0x45670123, "BOOT_KEY1"}, {0xcdef89ab, "BOOT_KEY2"},
  };
  for (const auto &item : writes) {
    int result = writeMemoryWordRetry(0x40022028, item.value, c);
    Serial.printf("MODE WRITE %s status=%d\n", item.name, result);
    if (result) { Serial.println("MODE STOP reason=key_failed"); return; }
  }
  const uint32_t requested = boot ? 0x4000 : 0;
  int result = writeMemoryWordRetry(0x4002200c, requested, c);
  Serial.printf("MODE WRITE value=0x%08lx status=%d\n", (unsigned long)requested, result);
  if (result) { Serial.println("MODE STOP reason=mode_write_failed"); return; }
  uint32_t readback = 0;
  result = readMemoryWord(0x4002200c, &readback, c);
  Serial.printf("MODE READBACK status=%d STATR=0x%08lx\n", result, (unsigned long)readback);
  if (result || (readback & 0x4000u) != requested) {
    Serial.println("MODE STOP reason=readback_mismatch"); return;
  }
  Serial.println("NDMRESET ASSERT"); Serial.flush();
  writeDmi(DMCONTROL, 3, c);
  delay(10);
  writeDmi(DMCONTROL, 1, c);
  writeDmi(DMCONTROL, 1, c);
  Serial.println("NDMRESET RELEASE");
}

void setup() { e123_setup(); }
void loop() {
  if (!Serial.available()) return;
  const int command = Serial.read();
  if (command == '?') {
    Serial.println("# EXP E126 swio-ndmreset-boot-hid");
    Serial.println("READY commands=U,B");
  } else if (command == 'U') {
    Serial.println("USER BEGIN"); switchMode(false); Serial.println("USER END");
  } else if (command == 'B') {
    Serial.println("BOOT BEGIN"); switchMode(true); Serial.println("BOOT END");
  }
}
