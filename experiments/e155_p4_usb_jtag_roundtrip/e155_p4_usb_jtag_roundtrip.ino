// E155: USB-Serial/JTAG echo server for round-trip and throughput measurement.
// Plan and report: README.ja.md
#include <Arduino.h>

static bool gBinary = false;
static uint8_t gBuf[4096 + 2];
static size_t gHave = 0;
static uint32_t gFrames = 0, gBytes = 0;

void setup() {
  Serial.setRxBufferSize(8192);
  Serial.setTxBufferSize(8192);
  Serial.begin(115200);
}

static void textMode() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line == "?") {
    Serial.printf("# EXP E155 v1 git=%s probe=esp32p4_x035 target=none rx=8192 tx=8192 build=%s\n",
                  BANNER_GIT, __DATE__ " " __TIME__);
    Serial.println("ECHO READY");
    Serial.flush();
    gBinary = true; gHave = 0; gFrames = 0; gBytes = 0;
  }
}

static void binaryMode() {
  const int avail = Serial.available();
  if (avail <= 0) return;
  const size_t room = sizeof(gBuf) - gHave;
  const size_t n = Serial.read(gBuf + gHave, avail < (int)room ? avail : room);
  gHave += n;
  for (;;) {
    if (gHave < 2) return;
    const size_t len = gBuf[0] | (size_t(gBuf[1]) << 8);
    if (len == 0) {
      gBinary = false;
      Serial.printf("ECHO END frames=%lu bytes=%lu\n", (unsigned long)gFrames, (unsigned long)gBytes);
      gHave = 0;
      return;
    }
    if (len > 4096) { gBinary = false; Serial.println("ECHO ERROR oversize"); gHave = 0; return; }
    if (gHave < len + 2) return;
    Serial.write(gBuf, len + 2);
    ++gFrames; gBytes += len + 2;
    memmove(gBuf, gBuf + len + 2, gHave - (len + 2));
    gHave -= len + 2;
  }
}

void loop() { if (gBinary) binaryMode(); else textMode(); }
