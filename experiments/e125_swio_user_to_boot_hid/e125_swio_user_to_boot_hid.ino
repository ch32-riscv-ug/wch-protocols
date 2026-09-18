#define E124_EMBEDDED
#include "../e124_swio_boot_hid_reenumerate/e124_swio_boot_hid_reenumerate.ino"

static void resetToUser() {
  pinMode(kPin, INPUT_PULLUP);
  delay(2);
  const int idle = digitalRead(kPin);
  Serial.printf("USER IDLE gpio16=%d\n", idle);
  if (!idle) { Serial.println("USER STOP reason=idle_low"); return; }

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
  Serial.printf("USER ATTACH status=%d DMCFGR=0x%08lx\n", result,
                static_cast<unsigned long>(cfgr));
  if (result || (cfgr & 0xffff0000u) != 0x5aa50000u) {
    Serial.println("USER STOP reason=attach_failed"); return;
  }

  uint32_t dmstatus = 0;
  result = haltTarget(c, &dmstatus);
  Serial.printf("USER HALT status=%d DMSTATUS=0x%08lx\n", result,
                static_cast<unsigned long>(dmstatus));
  if (result) { Serial.println("USER STOP reason=halt_failed"); return; }
  result = prepareWordWriter(c);
  Serial.printf("USER WRITER status=%d\n", result);
  if (result) { Serial.println("USER STOP reason=writer_failed"); return; }

  const struct { uint32_t address; uint32_t value; const char *name; } writes[] = {
    {0x40022028, 0x45670123, "BOOT_KEY1"},
    {0x40022028, 0xcdef89ab, "BOOT_KEY2"},
    {0x4002200c, 0x00000000, "USER_MODE"},
  };
  for (const auto &item : writes) {
    result = writeMemoryWord(item.address, item.value, c, true);
    Serial.printf("USER WRITE %s status=%d\n", item.name, result);
    if (result) { Serial.println("USER STOP reason=register_write_failed"); return; }
  }
  Serial.println("USER RESET REQUEST");
  Serial.flush();
  writeMemoryWord(0xe000e048, 0xbeef0080, c, false);
  delay(20);
  Serial.println("USER RESET SENT");
}

#ifndef E125_EMBEDDED
void setup() { e123_setup(); }

void loop() {
  if (!Serial.available()) return;
  const int command = Serial.read();
  if (command == '?') {
    Serial.println("# EXP E125 swio-user-to-boot-hid");
    Serial.println("READY commands=U,B");
  } else if (command == 'U') {
    Serial.println("USER BEGIN");
    resetToUser();
    Serial.println("USER END");
  } else if (command == 'B') {
    Serial.println("BOOT BEGIN");
    bootHid();
    Serial.println("BOOT END");
  }
}
#endif
