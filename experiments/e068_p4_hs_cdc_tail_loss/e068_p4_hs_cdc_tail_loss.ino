// E068: when the host does not receive the tail of a CDC transfer, is the tail
// lost or merely stuck? Plan and report: README.ja.md
//
// Reproduces the E067 condition that lost a tail most often: the sender pinned
// to core 0 with no capture running. After the transfer the device waits for a
// 'T' and answers with a short terminator, so the host can tell "the tail came
// out when prodded" from "only the prod's own bytes came out".

#include <Arduino.h>
#include <HWCDC.h>
#include <USB.h>
#include <USBCDC.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#include "esp32-hal-tinyusb.h"

#ifndef BANNER_GIT
#define BANNER_GIT "unknown"
#endif

static constexpr uint16_t kTestVid = 0x1209;
static constexpr uint16_t kTestPid = 0x0007;

static constexpr size_t kPatternBytes = 8u * 1024u * 1024u;
static constexpr size_t kUsbBytes = 4u * 1024u * 1024u;
static constexpr size_t kUsbChunk = 4096;
static constexpr uint32_t kTxTimeoutMs = 10000;
static constexpr BaseType_t kSenderCore = 0;
static constexpr size_t kTerminatorBytes = 16;

static HWCDC Console;
static USBCDC HsCdc(0);

static uint8_t *pattern;
static uint8_t terminator[kTerminatorBytes];

static bool host_armed;
static bool transfer_armed;
static bool prod_armed;

static SemaphoreHandle_t usb_done;
static volatile uint64_t usb_elapsed_us;
static volatile size_t usb_written;
static volatile uint32_t usb_short;

static char command[64];
static size_t command_length;

static void usb_task(void *) {
  size_t sent = 0;
  size_t written_total = 0;
  uint32_t short_writes = 0;
  const uint64_t started_us = esp_timer_get_time();
  while (sent < kUsbBytes) {
    const size_t want = (kUsbBytes - sent) < kUsbChunk ? (kUsbBytes - sent) : kUsbChunk;
    const size_t written = HsCdc.write(pattern + sent, want);
    written_total += written;
    if (written != want) {
      ++short_writes;
      if (written == 0) {
        break;
      }
    }
    sent += written;
  }
  HsCdc.flush();
  usb_elapsed_us = esp_timer_get_time() - started_us;
  usb_written = written_total;
  usb_short = short_writes;
  xSemaphoreGive(usb_done);
  vTaskDelete(nullptr);
}

static void report_banner(void) {
  Console.printf("# EXP E068 v1 git=%s probe=esp32p4_usb target=none build=%s %s\n", BANNER_GIT, __DATE__, __TIME__);
}

static void report_env(void) {
  Console.printf(
    "ENV chip=%s pattern_ready=%u usb_bytes=%lu usb_chunk=%lu sender_core=%d terminator_bytes=%u "
    "cdc_tx_bufsize=%d mounted=%u speed=%d\n",
    ESP.getChipModel(), pattern != nullptr ? 1U : 0U, static_cast<unsigned long>(kUsbBytes),
    static_cast<unsigned long>(kUsbChunk), static_cast<int>(kSenderCore), kTerminatorBytes,
    CONFIG_TINYUSB_CDC_TX_BUFSIZE, tud_mounted() ? 1U : 0U, static_cast<int>(tud_speed_get())
  );
}

static bool fill_pattern(void) {
  pattern = static_cast<uint8_t *>(heap_caps_malloc(kPatternBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (pattern == nullptr) {
    return false;
  }
  uint32_t *words = reinterpret_cast<uint32_t *>(pattern);
  for (size_t i = 0; i < kPatternBytes / sizeof(uint32_t); ++i) {
    words[i] = static_cast<uint32_t>(i);
  }
  for (size_t i = 0; i < kTerminatorBytes; i += 2) {
    terminator[i] = 0xE0;
    terminator[i + 1] = 0x68;
  }
  return true;
}

static void run_transfer(void) {
  usb_elapsed_us = 0;
  usb_written = 0;
  usb_short = 0;
  xTaskCreatePinnedToCore(usb_task, "e068_tx", 4096, nullptr, 5, nullptr, kSenderCore);
  xSemaphoreTake(usb_done, portMAX_DELAY);

  // The host may now be short of the tail. Wait for its prod rather than
  // sending anything on our own, so that "what the prod produced" is unambiguous.
  prod_armed = true;

  Console.printf(
    "SEND bytes=%lu written=%lu short=%lu elapsed_us=%llu\n", static_cast<unsigned long>(kUsbBytes),
    static_cast<unsigned long>(usb_written), static_cast<unsigned long>(usb_short),
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
    while (HsCdc.available()) {
      HsCdc.read();
    }
    prod_armed = false;
    transfer_armed = true;
    Console.println("CFG status=ok");
    Console.flush();
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
  USB.productName("OEP P4 HS Tail Loss Test");

  HsCdc.setTxTimeoutMs(kTxTimeoutMs);
  HsCdc.begin();
  USB.begin();
}

void loop() {
  if (HsCdc.available() > 0) {
    const int value = HsCdc.read();
    if (transfer_armed && value == 'G') {
      transfer_armed = false;
      run_transfer();
    } else if (prod_armed && value == 'T') {
      prod_armed = false;
      HsCdc.write(terminator, kTerminatorBytes);
      HsCdc.flush();
      Console.println("PROD sent=1");
      Console.flush();
    }
    return;
  }

  if (!host_armed) {
    if (millis() < 1000) {
      delay(10);
      return;
    }
    while (Console.available()) {
      Console.read();
    }
    Console.println("READY E068");
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
