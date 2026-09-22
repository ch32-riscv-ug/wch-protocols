// E160: OEP-style frame echo over the P4 HS vendor bulk interface (EspUsbDevice, buffered).
// Console (UART) carries the banner and the end line; the frames go over bulk only.
// Identity is E104's so the Windows usbipd binding survives (VID/PID/serial are the instance key).
#include "EspUsbDevice.h"
#ifndef E160_FLUSH_PER_FRAME
#define E160_FLUSH_PER_FRAME "0"
#endif
// build_config defines arrive as quoted strings (see E151 notes); decide at run time.
static const bool kFlushPerFrame = E160_FLUSH_PER_FRAME[0] == '1';

EspUsbDevice device;
EspUsbDeviceVendor Vendor(device);

static uint8_t gBuf[4096 + 2];
static size_t gHave = 0;
static uint32_t gFrames = 0, gBytes = 0;
static bool gEcho = false;

void setup() {
  Serial.begin(115200);
  delay(300);
  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4021;
  config.manufacturer = "wch-protocols";
  config.product = "E160 P4 HS bulk frame echo";
  config.serialNumber = "e104-p4-windows-v1";
  config.controller = EspUsbController::HighSpeed;
  config.webusbEnabled = true;
  const bool ok = device.begin(config);
  Serial.printf("# EXP E160 v1 git=%s probe=esp32p4_hs target=none usb_begin=%d write_capacity=%u flush_per_frame=%d build=%s\n",
                BANNER_GIT, ok ? 1 : 0, (unsigned)EspUsbDeviceVendor::writeCapacity(), kFlushPerFrame ? 1 : 0, __DATE__ " " __TIME__);
}

void loop() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n'); line.trim();
    if (line == "?") {
      Serial.printf("ECHO READY mounted=%d\n", Vendor.mounted() ? 1 : 0);
      gEcho = true; gHave = 0; gFrames = 0; gBytes = 0;
    }
  }
  if (!gEcho) return;
  const int avail = Vendor.available();
  if (avail > 0) {
    const size_t room = sizeof(gBuf) - gHave;
    gHave += Vendor.read(gBuf + gHave, (size_t)avail < room ? (size_t)avail : room);
  }
  for (;;) {
    if (gHave < 2) { Vendor.flush(); return; }
    const size_t len = gBuf[0] | (size_t(gBuf[1]) << 8);
    if (len == 0) {
      gEcho = false;
      Serial.printf("ECHO END frames=%lu bytes=%lu\n", (unsigned long)gFrames, (unsigned long)gBytes);
      gHave = 0;
      return;
    }
    if (len > 4096) { gEcho = false; Serial.println("ECHO ERROR oversize"); gHave = 0; return; }
    if (gHave < len + 2) { Vendor.flush(); return; }
    size_t off = 0;
    while (off < len + 2) off += Vendor.write(gBuf + off, len + 2 - off);
    if (kFlushPerFrame) Vendor.flush();  // A: one IN transfer per frame
    // B: otherwise coalesce - the loop flushes when this burst's input is exhausted (partial frame
    // or nothing left); the buffered build arms IN only on a full packet in between.
    ++gFrames; gBytes += len + 2;
    memmove(gBuf, gBuf + len + 2, gHave - (len + 2));
    gHave -= len + 2;
  }
}
