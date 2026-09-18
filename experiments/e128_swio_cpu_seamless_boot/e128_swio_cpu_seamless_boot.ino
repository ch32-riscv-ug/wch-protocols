#define E127_EMBEDDED
#include "../e127_uiap_seamless_boot_swio/e127_uiap_seamless_boot_swio.ino"

static const uint32_t kCpuReset[] = {
  0xe000e2b7, 0x04828293, 0xbeef0337, 0x08030313, 0x0062a023, 0x0000006f,
};

static const uint32_t kPrepareBoot[] = {
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
  0x0000006f,
};

static int injectWords(const uint32_t *words, size_t count, int c) {
  for (size_t i = 0; i < count; ++i) {
    int result = writeMemoryWordRetry(0x20000000u + i * 4u, words[i], c);
    if (result) return result;
  }
  return 0;
}

static int setDpc(uint32_t address, int c) {
  writeDmi(kDmAbstractAuto, 0, c);
  writeDmi(kData0, address, c);
  writeDmi(kDmCommand, 0x002307b1, c);  // write CSR dpc from DATA0
  return waitAbstract(c);
}

static void resumeCpu(int c) {
  writeDmi(DMCONTROL, 0x40000001, c);
  writeDmi(DMCONTROL, 0x40000001, c);
  writeDmi(DMCONTROL, 1, c);
}

static bool runPayload(const uint32_t *words, size_t count, const char *name, int c) {
  if (!attachHaltWriter(c)) return false;
  int result = injectWords(words, count, c);
  Serial.printf("CPU INJECT name=%s words=%u status=%d\n", name,
                static_cast<unsigned>(count), result);
  Serial.flush();
  if (result) return false;
  result = setDpc(0x20000000, c);
  Serial.printf("CPU DPC name=%s address=0x20000000 status=%d\n", name, result);
  Serial.flush();
  if (result) return false;
  Serial.printf("CPU RESUME name=%s\n", name); Serial.flush();
  resumeCpu(c);
  return true;
}

static void cpuSeamlessBoot() {
  const int c = 10;
  if (!runPayload(kCpuReset, sizeof(kCpuReset) / 4, "nvic_reset", c)) {
    Serial.println("CPU STOP reason=reset_payload_failed"); return;
  }
  delay(150);
  if (!runPayload(kPrepareBoot, sizeof(kPrepareBoot) / 4, "prepare_boot", c)) {
    Serial.println("CPU STOP reason=prepare_payload_failed"); return;
  }
  Serial.println("CPU USB_DETACH_WAIT ms=100"); Serial.flush();
  delay(100);
  Serial.println("CPU HWRESET ASSERT pin=23"); Serial.flush();
  hardwareResetTarget();
  Serial.println("CPU HWRESET RELEASE pin=23");
}

#ifndef E128_EMBEDDED
void setup() {
  pinMode(kTargetResetPin, INPUT);
  e123_setup();
}

void loop() {
  if (!Serial.available()) return;
  const int command = Serial.read();
  if (command == '?') {
    Serial.println("# EXP E128 swio-cpu-seamless-boot");
    Serial.println("READY commands=BR");
  } else if (command == 'B') {
    Serial.println("CPU BEGIN"); cpuSeamlessBoot(); Serial.println("CPU END");
  } else if (command == 'R') {
    Serial.println("RESET ASSERT pin=23"); Serial.flush();
    hardwareResetTarget();
    Serial.println("RESET RELEASE pin=23");
  }
}
#endif
