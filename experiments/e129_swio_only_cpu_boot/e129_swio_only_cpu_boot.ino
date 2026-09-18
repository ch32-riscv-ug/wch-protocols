#define E128_EMBEDDED
#include "../e128_swio_cpu_seamless_boot/e128_swio_cpu_seamless_boot.ino"

static const uint32_t kPrepareBootAndReset[] = {
  0x400222b7, 0x00428293, 0x45670337, 0x12330313, 0x0062a023,
  0xcdef9337, 0x9ab30313, 0x0062a023,
  0x400222b7, 0x02428293, 0x45670337, 0x12330313, 0x0062a023,
  0xcdef9337, 0x9ab30313, 0x0062a023,
  0x400222b7, 0x02828293, 0x45670337, 0x12330313, 0x0062a023,
  0xcdef9337, 0x9ab30313, 0x0062a023, 0x400222b7, 0x00c28293,
  0x0002a303, 0xffffc3b7, 0xfff38393, 0x00737333, 0x000043b7,
  0x00736333, 0x0062a023, 0x400212b7, 0x01828293, 0x0002a303,
  0x02036313, 0x0062a023, 0x400112b7, 0x40028293, 0x0002a303,
  0xfff103b7, 0xfff38393, 0x00737333, 0x000303b7, 0x00736333,
  0x0062a023, 0x400112b7, 0x41428293, 0x01000313, 0x0062a023,
  0x004c52b7, 0xb4028293, 0xfff28293, 0xfe029ee3, 0xe000e2b7,
  0x04828293, 0xbeef0337, 0x08030313, 0x0062a023, 0x0000006f,
};

static const uint32_t kNormalizeUserReset[] = {
  0x400222b7, 0x00428293, 0x45670337, 0x12330313, 0x0062a023,
  0xcdef9337, 0x9ab30313, 0x0062a023,
  0x400222b7, 0x02428293, 0x45670337, 0x12330313, 0x0062a023,
  0xcdef9337, 0x9ab30313, 0x0062a023,
  0x400222b7, 0x02828293, 0x45670337, 0x12330313, 0x0062a023,
  0xcdef9337, 0x9ab30313, 0x0062a023,
  0x400222b7, 0x00c28293, 0x0002a303, 0xffffc3b7, 0xfff38393,
  0x00737333, 0x0062a023, 0xe000e2b7, 0x04828293, 0xbeef0337,
  0x08030313, 0x0062a023, 0x0000006f,
};

static void normalizeByCpuReset() {
  const int c = 10;
  if (!runPayload(kNormalizeUserReset, sizeof(kNormalizeUserReset) / 4,
                  "normalize_user_reset", c)) {
    Serial.println("SWIO STOP reason=normalize_failed");
  }
}
static void swioOnlyBoot() {
  const int c = 10;
  if (!runPayload(kPrepareBootAndReset, sizeof(kPrepareBootAndReset) / 4,
                  "prepare_boot_and_reset", c)) {
    Serial.println("SWIO STOP reason=boot_payload_failed"); return;
  }
  Serial.println("SWIO CPU_RESET_ARMED delay_loops=5000000");
}

static void inspectBootStatus() {
  const int c = 10;
  if (!attachHaltWriter(c)) {
    Serial.println("STATUS STOP reason=attach_failed"); return;
  }
  uint32_t statr = 0;
  const int result = readThenRestoreWriter(0x4002200c, &statr, c);
  Serial.printf("STATUS STATR status=%d value=0x%08lx BOOT_LOCK=%u BOOT_MODE=%u BOOT_STATUS=%u\n",
                result, (unsigned long)statr, (unsigned)((statr >> 15) & 1u),
                (unsigned)((statr >> 14) & 1u), (unsigned)((statr >> 13) & 1u));
}

static bool waitFlashIdle(int c, uint32_t *lastStatus = nullptr) {
  uint32_t statr = 0;
  for (int attempt = 0; attempt < 400; ++attempt) {
    const int readResult = readMemoryWord(0x4002200c, &statr, c);
    if (readResult) continue;
    if (!(statr & 1u)) {
      if (lastStatus) *lastStatus = statr;
      for (int restore = 0; restore < 5; ++restore) {
        if (!prepareWordWriter(c)) return true;
      }
      return false;
    }
  }
  if (lastStatus) *lastStatus = statr;
  return false;
}

static bool flashWriteWord(uint32_t address, uint32_t value, int c) {
  for (int attempt = 1; attempt <= 5; ++attempt) {
    writeDmi(kData1, address, c);
    writeDmi(kData0, value, c);
    uint32_t addressRead = 0, valueRead = 0;
    const int addressStatus = readDmi(kData1, &addressRead, c);
    const int valueStatus = readDmi(kData0, &valueRead, c);
    if (addressStatus || valueStatus || addressRead != address || valueRead != value) {
      Serial.printf("FLASH DMI_RETRY attempt=%d address=0x%08lx address_read=0x%08lx value=0x%08lx value_read=0x%08lx\n",
                    attempt, (unsigned long)address, (unsigned long)addressRead,
                    (unsigned long)value, (unsigned long)valueRead);
      continue;
    }
    writeDmi(kDmCommand, 0x00240000, c);
    if (!waitAbstract(c)) {
      bool executed = false;
      for (int poll = 0; poll < 5; ++poll) {
        uint32_t nextAddress = 0;
        if (!readDmi(kData1, &nextAddress, c) && nextAddress == address + 4u) {
          executed = true;
          break;
        }
      }
      if (executed) return true;
      Serial.printf("FLASH COMMAND_RETRY attempt=%d address=0x%08lx\n",
                    attempt, (unsigned long)address);
    }
    prepareWordWriter(c);
  }
  return false;
}

static bool programFlashPage64(uint32_t address, const uint8_t *data) {
  const int c = 10;
  if ((address & 63u) || address < 0x08000000u || address >= 0x08004000u) {
    Serial.printf("FLASH STOP reason=bad_address address=0x%08lx\n",
                  (unsigned long)address);
    return false;
  }
  if (!attachHaltWriter(c)) {
    Serial.println("FLASH STOP reason=attach_failed");
    return false;
  }

  const struct { uint32_t address; uint32_t value; } unlock[] = {
    {0x40022004u, 0x45670123u}, {0x40022004u, 0xcdef89abu},
    {0x40022024u, 0x45670123u}, {0x40022024u, 0xcdef89abu},
  };
  for (const auto &item : unlock) {
    if (!flashWriteWord(item.address, item.value, c)) {
      Serial.println("FLASH STOP reason=unlock_failed");
      return false;
    }
  }

  uint32_t statr = 0;
  if (!waitFlashIdle(c, &statr) ||
      !flashWriteWord(0x40022010u, 0x00020000u, c) ||
      !flashWriteWord(0x40022014u, address, c) ||
      !flashWriteWord(0x40022010u, 0x00020040u, c) ||
      !waitFlashIdle(c, &statr)) {
    Serial.printf("FLASH STOP reason=erase_failed STATR=0x%08lx\n",
                  (unsigned long)statr);
    return false;
  }

  if (!flashWriteWord(0x40022010u, 0x00010000u, c) ||
      !flashWriteWord(0x40022010u, 0x00090000u, c) ||
      !waitFlashIdle(c, &statr)) {
    Serial.printf("FLASH STOP reason=buffer_reset_failed STATR=0x%08lx\n",
                  (unsigned long)statr);
    return false;
  }
  for (uint32_t offset = 0; offset < 64; offset += 4) {
    const uint32_t word = (uint32_t)data[offset] |
                          ((uint32_t)data[offset + 1] << 8) |
                          ((uint32_t)data[offset + 2] << 16) |
                          ((uint32_t)data[offset + 3] << 24);
    if (!flashWriteWord(address + offset, word, c) ||
        !flashWriteWord(0x40022010u, 0x00050000u, c) ||
        !waitFlashIdle(c, &statr)) {
      Serial.printf("FLASH STOP reason=buffer_load_failed offset=%lu STATR=0x%08lx\n",
                    (unsigned long)offset, (unsigned long)statr);
      return false;
    }
  }
  if (!flashWriteWord(0x40022014u, address, c) ||
      !flashWriteWord(0x40022010u, 0x00010040u, c) ||
      !waitFlashIdle(c, &statr) ||
      !flashWriteWord(0x40022010u, 0, c)) {
    Serial.printf("FLASH STOP reason=program_failed STATR=0x%08lx\n",
                  (unsigned long)statr);
    return false;
  }

  for (uint32_t offset = 0; offset < 64; offset += 4) {
    const uint32_t expected = (uint32_t)data[offset] |
                              ((uint32_t)data[offset + 1] << 8) |
                              ((uint32_t)data[offset + 2] << 16) |
                              ((uint32_t)data[offset + 3] << 24);
    uint32_t actual = 0;
    int readResult = -1;
    for (int attempt = 0; attempt < 3; ++attempt) {
      readResult = readMemoryWord(address + offset, &actual, c);
      if (!readResult && actual == expected) break;
    }
    if (readResult || actual != expected) {
      Serial.printf("FLASH STOP reason=verify_failed offset=%lu expected=0x%08lx actual=0x%08lx\n",
                    (unsigned long)offset, (unsigned long)expected,
                    (unsigned long)actual);
      return false;
    }
  }
  Serial.printf("FLASH OK address=0x%08lx STATR=0x%08lx\n",
                (unsigned long)address, (unsigned long)statr);
  return true;
}

static void receiveFlashPage() {
  uint8_t packet[72];
  const size_t received = Serial.readBytes(packet, sizeof(packet));
  if (received != sizeof(packet)) {
    Serial.printf("FLASH STOP reason=short_packet received=%u expected=%u\n",
                  (unsigned)received, (unsigned)sizeof(packet));
    return;
  }
  const uint32_t address = (uint32_t)packet[0] |
                           ((uint32_t)packet[1] << 8) |
                           ((uint32_t)packet[2] << 16) |
                           ((uint32_t)packet[3] << 24);
  uint32_t crc = 0xffffffffu;
  for (size_t index = 0; index < 68; ++index) {
    crc ^= packet[index];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
  }
  crc = ~crc;
  const uint32_t expectedCrc = (uint32_t)packet[68] |
                               ((uint32_t)packet[69] << 8) |
                               ((uint32_t)packet[70] << 16) |
                               ((uint32_t)packet[71] << 24);
  if (crc != expectedCrc) {
    Serial.printf("FLASH STOP reason=packet_crc actual=0x%08lx expected=0x%08lx\n",
                  (unsigned long)crc, (unsigned long)expectedCrc);
    return;
  }
  programFlashPage64(address, packet + 4);
}

static void receiveReadPage() {
  uint8_t packet[4];
  const size_t received = Serial.readBytes(packet, sizeof(packet));
  if (received != sizeof(packet)) {
    Serial.printf("READ STOP reason=short_packet received=%u expected=%u\n",
                  (unsigned)received, (unsigned)sizeof(packet));
    return;
  }
  const uint32_t address = (uint32_t)packet[0] |
                           ((uint32_t)packet[1] << 8) |
                           ((uint32_t)packet[2] << 16) |
                           ((uint32_t)packet[3] << 24);
  const int c = 10;
  if ((address & 63u) || !attachHaltWriter(c)) {
    Serial.println("READ STOP reason=prepare_failed");
    return;
  }
  Serial.printf("READ PAGE address=0x%08lx", (unsigned long)address);
  for (uint32_t offset = 0; offset < 64; offset += 4) {
    uint32_t value = 0;
    const int result = readMemoryWord(address + offset, &value, c);
    if (result) {
      Serial.printf("\nREAD STOP reason=read_failed offset=%lu status=%d\n",
                    (unsigned long)offset, result);
      return;
    }
    Serial.printf(" %08lx", (unsigned long)value);
  }
  Serial.println("\nREAD OK");
}

void e129_setup() {
  // GPIO23 remains Hi-Z for the entire experiment.
  pinMode(kTargetResetPin, INPUT);
  e123_setup();
}

void e129_loop() {
  if (!Serial.available()) return;
  const int command = Serial.read();
  if (command == '?') {
    Serial.println("# EXP E129 swio-only-cpu-boot");
    Serial.println("READY commands=NBRHSWV");
  } else if (command == 'N') {
    Serial.println("NORMALIZE BEGIN"); normalizeByCpuReset(); Serial.println("NORMALIZE END");
  } else if (command == 'B') {
    Serial.println("SWIO BOOT BEGIN"); swioOnlyBoot(); Serial.println("SWIO BOOT END");
  } else if (command == 'R') {
    Serial.println("RESET ASSERT pin=23"); Serial.flush();
    hardwareResetTarget();
    Serial.println("RESET RELEASE pin=23");
  } else if (command == 'H') {
    Serial.println("HW BOOT BEGIN"); cpuSeamlessBoot(); Serial.println("HW BOOT END");
  } else if (command == 'S') {
    inspectBootStatus();
  } else if (command == 'W') {
    receiveFlashPage();
  } else if (command == 'V') {
    receiveReadPage();
  }
}

#ifndef E129_EMBEDDED
void setup() { e129_setup(); }
void loop() { e129_loop(); }
#endif
