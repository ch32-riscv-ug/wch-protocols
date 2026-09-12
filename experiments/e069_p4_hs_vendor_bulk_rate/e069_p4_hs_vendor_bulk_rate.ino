// E069: how fast PSRAM reaches the host over a vendor bulk endpoint, and
// whether the packets E068 saw disappear also disappear here.
// Plan and report: README.ja.md
//
// One vendor interface and nothing else on the OTG HS port, so the host talks
// WinUSB and decides its own URB size. webUSB is enabled only to make the core
// emit the BOS and MS OS 2.0 descriptor set that binds WinUSB automatically.

#include <Arduino.h>
#include <HWCDC.h>
#include <USB.h>
#include <USBVendor.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#include "esp32-hal-tinyusb.h"

#ifndef BANNER_GIT
#define BANNER_GIT "unknown"
#endif

static constexpr uint16_t kTestVid = 0x1209;
static constexpr uint16_t kTestPid = 0x0008;

static constexpr size_t kPatternBytes = 64u * 1024u;  // internal RAM, sent repeatedly
static constexpr size_t kUsbBytes = 4u * 1024u * 1024u;
static constexpr BaseType_t kSenderCore = 0;

static HWCDC Console;
static USBVendor HsVendor;

static uint8_t *pattern;
static bool host_armed;

static SemaphoreHandle_t usb_done;
static volatile uint64_t usb_elapsed_us;
static volatile size_t usb_written;
static volatile uint32_t usb_stalls;

static char command[64];
static size_t command_length;

// USBVendor::write() accepts only what the FIFO has room for and returns that
// count, so the loop has to keep offering the rest rather than assume a short
// write means failure.
static void usb_task(void *) {
  size_t sent = 0;
  uint32_t stalls = 0;
  const uint64_t started_us = esp_timer_get_time();
  size_t offset = 0;
  while (sent < kUsbBytes) {
    const size_t room = kPatternBytes - offset;
    const size_t want = (kUsbBytes - sent) < room ? (kUsbBytes - sent) : room;
    const size_t written = HsVendor.write(pattern + offset, want);
    if (written == 0) {
      ++stalls;
      HsVendor.flush();
      taskYIELD();
      continue;
    }
    sent += written;
    offset = (offset + written) % kPatternBytes;
  }
  HsVendor.flush();
  usb_elapsed_us = esp_timer_get_time() - started_us;
  usb_written = sent;
  usb_stalls = stalls;
  xSemaphoreGive(usb_done);
  vTaskDelete(nullptr);
}

static void report_banner(void) {
  Console.printf("# EXP E069 v1 git=%s probe=esp32p4_usb target=none build=%s %s\n", BANNER_GIT, __DATE__, __TIME__);
}

static void report_env(void) {
  Console.printf(
    "ENV chip=%s pattern_ready=%u psram_found=%u psram_size=%lu psram_free=%lu usb_bytes=%lu sender_core=%d "
    "vendor_tx_bufsize=%d ep_size=%d webusb=%u usb_version=%04x dev_class=%02x mounted=%u speed=%d\n",
    ESP.getChipModel(), pattern != nullptr ? 1U : 0U, psramFound() ? 1U : 0U,
    static_cast<unsigned long>(ESP.getPsramSize()), static_cast<unsigned long>(ESP.getFreePsram()),
    static_cast<unsigned long>(kUsbBytes), static_cast<int>(kSenderCore), CONFIG_TINYUSB_VENDOR_TX_BUFSIZE,
    CFG_TUD_ENDPOINT_SIZE, USB.webUSB() ? 1U : 0U, USB.usbVersion(), USB.usbClass(), tud_mounted() ? 1U : 0U,
    static_cast<int>(tud_speed_get())
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
  xTaskCreatePinnedToCore(usb_task, "e069_tx", 4096, nullptr, 5, nullptr, kSenderCore);
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
    while (HsVendor.available()) {
      HsVendor.read();
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

  USB.VID(kTestVid);
  USB.PID(kTestPid);
  USB.manufacturerName("Open Embedded Probe (TEST ONLY)");
  USB.productName("OEP P4 HS Vendor Bulk Test");
  // Not for the browser: this is what makes the core serve the BOS and the
  // MS OS 2.0 descriptor set whose WINUSB compatible ID binds the driver.
  // The core gives every non-S3 target the serial string "0" (E063), so the
  // Windows device instance is USB\VID&PID\0 and survives a bcdDevice change.
  // A failed driver match sticks to that instance and is never re-probed, so
  // this experiment takes a serial of its own.
  USB.serialNumber("E069-A");
  USB.usbClass(0x00);
  USB.usbSubClass(0x00);
  USB.usbProtocol(0x00);
  USB.firmwareVersion(0x0201);
  USB.webUSB(true);

  HsVendor.begin();
  USB.begin();
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
    Console.println("READY E069");
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
