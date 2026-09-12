// E081: does Windows bind WinUSB once the MS OS 2.0 set is flat? The same vendor
// bulk device as E071, with one thing under test: the shape of the MS OS 2.0
// descriptor set. Plan and report: README.ja.md
//
// MS_OS_20_LAYOUT picks it at build time, and each layout gets its own serial
// number. That matters more than it looks: Windows keys a device instance on
// VID + PID + serial and never re-evaluates a driver match that already failed
// on it (E062), so reusing one serial across the two builds would return the
// first verdict for both and prove nothing.

#include <Arduino.h>
#include <HWCDC.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#include "EspUsbDevice.h"

#ifndef BANNER_GIT
#define BANNER_GIT "unknown"
#endif
// 0 = AUTO (flat, since this device has exactly one interface), 2 = SUBSETS.
#ifndef MS_OS_20_LAYOUT
#define MS_OS_20_LAYOUT 0
#endif
#ifndef USB_SERIAL_NUMBER
#define USB_SERIAL_NUMBER "E081-A"
#endif

static constexpr uint16_t kTestVid = 0x1209;
// Same PID as E069; the serial is what separates the instances, and this
// experiment deliberately uses serials Windows has never seen.
static constexpr uint16_t kTestPid = 0x0008;
static constexpr uint16_t kEndpointSize = 512;

static constexpr size_t kPatternBytes = 64u * 1024u;
static constexpr size_t kUsbBytes = 4u * 1024u * 1024u;
static constexpr BaseType_t kSenderCore = 0;

static HWCDC Console;
static EspUsbDevice device;
static EspUsbDeviceVendor UsbVendor(device, kEndpointSize);

static uint8_t *pattern;
static bool host_armed;
static bool usb_ready;

static SemaphoreHandle_t usb_done;
static volatile uint64_t usb_elapsed_us;
static volatile size_t usb_written;
static volatile uint32_t usb_stalls;

static char command[64];
static size_t command_length;

static void usb_task(void *) {
  size_t sent = 0;
  size_t offset = 0;
  uint32_t stalls = 0;
  const uint64_t started_us = esp_timer_get_time();
  while (sent < kUsbBytes) {
    const size_t room = kPatternBytes - offset;
    const size_t want = (kUsbBytes - sent) < room ? (kUsbBytes - sent) : room;
    const size_t written = UsbVendor.write(pattern + offset, want);
    if (written == 0) {
      ++stalls;
      UsbVendor.flush();
      taskYIELD();
      continue;
    }
    sent += written;
    offset = (offset + written) % kPatternBytes;
  }
  UsbVendor.flush();
  usb_elapsed_us = esp_timer_get_time() - started_us;
  usb_written = sent;
  usb_stalls = stalls;
  xSemaphoreGive(usb_done);
  vTaskDelete(nullptr);
}

static void report_banner(void) {
  Console.printf("# EXP E081 v1 git=%s probe=esp32p4_usb target=none build=%s %s\n", BANNER_GIT, __DATE__, __TIME__);
}

static void report_env(void) {
  Console.printf(
    "ENV chip=%s stack=espusbdevice lib=%s serial=%s ms_os_20_layout=%d ms_os_20_len=%u subsets=%u "
    "pattern_ready=%u usb_bytes=%lu ep_size=%u usb_ready=%u mounted=%u\n",
    ESP.getChipModel(), ESPUSBDEVICE_VERSION_STR, USB_SERIAL_NUMBER, MS_OS_20_LAYOUT,
    device.microsoftOs20DescriptorLength(), device.microsoftOs20UsesSubsets() ? 1U : 0U,
    pattern != nullptr ? 1U : 0U, static_cast<unsigned long>(kUsbBytes), kEndpointSize, usb_ready ? 1U : 0U,
    UsbVendor.mounted() ? 1U : 0U
  );
}

static bool fill_pattern(void) {
  pattern = static_cast<uint8_t *>(heap_caps_malloc(kPatternBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (pattern == nullptr) {
    return false;
  }
  uint32_t *words = reinterpret_cast<uint32_t *>(pattern);
  for (size_t i = 0; i < kPatternBytes / sizeof(uint32_t); ++i) {
    words[i] = static_cast<uint32_t>(i);
  }
  return true;
}

static void run_transfer(void) {
  usb_elapsed_us = 0;
  usb_written = 0;
  usb_stalls = 0;
  xTaskCreatePinnedToCore(usb_task, "e081_tx", 4096, nullptr, 5, nullptr, kSenderCore);
  xSemaphoreTake(usb_done, portMAX_DELAY);

  Console.printf(
    "SEND bytes=%lu written=%lu stalls=%lu elapsed_us=%llu\n", static_cast<unsigned long>(kUsbBytes),
    static_cast<unsigned long>(usb_written), static_cast<unsigned long>(usb_stalls),
    static_cast<unsigned long long>(usb_elapsed_us)
  );
  Console.flush();
}

static void handle_command(void) {
  if (command[0] == '?') {
    report_banner();
    report_env();
    Console.flush();
    return;
  }
  if (command[0] == 'C') {
    while (UsbVendor.available()) {
      UsbVendor.read();
    }
    Console.println("CFG status=ok");
    Console.flush();
    run_transfer();
    return;
  }
  Console.printf("CFG status=unknown\n");
  Console.flush();
}

void setup() {
  Console.begin();
  fill_pattern();
  usb_done = xSemaphoreCreateBinary();

  EspUsbDeviceConfig config;
  config.vid = kTestVid;
  config.pid = kTestPid;
  config.manufacturer = "Open Embedded Probe (TEST ONLY)";
  config.product = "OEP P4 WinUSB Bind Test";
  // A serial Windows has not seen before, per layout. See the file header.
  config.serialNumber = USB_SERIAL_NUMBER;
  config.selfPowered = true;
  config.maxPowerMilliamps = 500;
  // Same reason as E069: this is what emits the BOS and MS OS 2.0 set.
  config.webusbEnabled = true;
  config.msOs20Layout = static_cast<EspUsbDeviceMsOs20Layout>(MS_OS_20_LAYOUT);
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
    Console.println("READY E081");
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
    if (value < 0) {
      break;
    }
    const char received = static_cast<char>(value);
    if (received == '\r') {
      continue;
    }
    if (received != '\n') {
      if (command_length < sizeof(command) - 1) {
        command[command_length++] = received;
      }
      continue;
    }
    command[command_length] = '\0';
    if (command_length > 0) {
      handle_command();
    }
    command_length = 0;
  }
}
