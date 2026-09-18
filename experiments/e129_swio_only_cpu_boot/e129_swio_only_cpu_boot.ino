#define E128_EMBEDDED
#include "../e128_swio_cpu_seamless_boot/e128_swio_cpu_seamless_boot.ino"

static const uint32_t kPrepareBootAndReset[] = {
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

static void normalizeByCpuReset() {
  const int c = 10;
  if (!runPayload(kCpuReset, sizeof(kCpuReset) / 4, "normalize_reset", c)) {
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

void setup() {
  // GPIO23 remains Hi-Z for the entire experiment.
  pinMode(kTargetResetPin, INPUT);
  e123_setup();
}

void loop() {
  if (!Serial.available()) return;
  const int command = Serial.read();
  if (command == '?') {
    Serial.println("# EXP E129 swio-only-cpu-boot");
    Serial.println("READY commands=NB");
  } else if (command == 'N') {
    Serial.println("NORMALIZE BEGIN"); normalizeByCpuReset(); Serial.println("NORMALIZE END");
  } else if (command == 'B') {
    Serial.println("SWIO BOOT BEGIN"); swioOnlyBoot(); Serial.println("SWIO BOOT END");
  }
}
