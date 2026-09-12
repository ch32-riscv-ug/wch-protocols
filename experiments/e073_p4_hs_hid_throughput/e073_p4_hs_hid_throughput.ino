// E073 device side: how fast can a HID input endpoint stream on USB HS?
// Plan and report: README.ja.md
//
// The repo's channel notes quote HID at 64 kB/s, which is the full-speed figure
// (64 B every 1 ms). A high-speed interrupt endpoint polls every 125 us and may
// carry up to 1024 B, so the ceiling should be far higher. This measures what
// the stack actually delivers.

#include <Arduino.h>
#include <HWCDC.h>
#include <esp_timer.h>

#include "EspUsbDevice.h"

#ifndef BANNER_GIT
#define BANNER_GIT "unknown"
#endif
#ifndef HID_REPORT_BYTES
#define HID_REPORT_BYTES 63
#endif

static constexpr uint16_t kTestVid = 0x1209;
static constexpr uint16_t kTestPid = 0x000A;
static constexpr uint32_t kReports = 20000;

static HWCDC Console;
static EspUsbDevice device;
static EspUsbDeviceHidVendor Hid(device, HID_REPORT_BYTES);

static uint8_t payload[HID_REPORT_BYTES];
static bool host_armed;
static bool usb_ready;
static char command[64];
static size_t command_length;

static void run_burst(void) {
  uint32_t sent = 0;
  uint32_t failed = 0;
  const uint64_t started = esp_timer_get_time();
  for (uint32_t i = 0; i < kReports; ++i) {
    payload[0] = static_cast<uint8_t>(i);
    payload[1] = static_cast<uint8_t>(i >> 8);
    if (Hid.sendInput(payload, sizeof(payload), 1000)) {
      ++sent;
    } else {
      ++failed;
    }
  }
  const uint64_t elapsed = esp_timer_get_time() - started;
  const uint32_t bytes = sent * static_cast<uint32_t>(sizeof(payload));
  Console.printf("SEND reports=%lu failed=%lu report_bytes=%u bytes=%lu elapsed_us=%llu\n",
                 (unsigned long)sent, (unsigned long)failed, (unsigned)sizeof(payload), (unsigned long)bytes,
                 (unsigned long long)elapsed);
  Console.flush();
}

void setup() {
  Console.begin();
  for (size_t i = 0; i < sizeof(payload); ++i) {
    payload[i] = static_cast<uint8_t>(i);
  }

  EspUsbDeviceConfig config;
  config.vid = kTestVid;
  config.pid = kTestPid;
  config.manufacturer = "Open Embedded Probe (TEST ONLY)";
  config.product = "OEP P4 HS HID Throughput";
  config.serialNumber = "E073-A";
  config.selfPowered = true;
  config.maxPowerMilliamps = 500;
  config.controller = EspUsbController::HighSpeed;
  usb_ready = device.begin(config);
}

void loop() {
  if (!host_armed) {
    if (millis() < 1000) {
      delay(10);
      return;
    }
    while (Console.available()) {
      Console.read();
    }
    Console.println("READY E073");
    Console.flush();
    host_armed = true;
    return;
  }
  if (!Console.available()) {
    delay(1);
    return;
  }
  while (Console.available()) {
    const int value = Console.read();
    if (value < 0) break;
    const char received = static_cast<char>(value);
    if (received == '\r') continue;
    if (received != '\n') {
      if (command_length < sizeof(command) - 1) command[command_length++] = received;
      continue;
    }
    command[command_length] = '\0';
    if (command_length > 0) {
      if (command[0] == '?') {
        Console.printf("# EXP E073 v1 git=%s probe=esp32p4_usb target=none build=%s %s\n", BANNER_GIT, __DATE__, __TIME__);
        Console.printf("ENV chip=%s stack=espusbdevice lib=%s report_bytes=%u reports=%lu usb_ready=%u\n",
                       ESP.getChipModel(), ESPUSBDEVICE_VERSION_STR, (unsigned)sizeof(payload),
                       (unsigned long)kReports, usb_ready ? 1U : 0U);
        Console.flush();
      } else if (command[0] == 'C') {
        Console.println("CFG status=ok");
        Console.flush();
        run_burst();
      }
    }
    command_length = 0;
  }
}
