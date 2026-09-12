// E064: how fast PSRAM reaches the host over the USB 2.0 OTG HS CDC.
// Plan and report: README.ja.md
//
// The console (USB-Serial-JTAG) carries only the configuration and the result;
// the measured bytes go out of the OTG HS CDC. The transfer starts when the
// host writes 'G' on that CDC, so the reader is always listening first.

#include <Arduino.h>
#include <HWCDC.h>
#include <USB.h>
#include <USBCDC.h>
#include <esp_heap_caps.h>
#include <soc/soc_caps.h>

#include "esp32-hal-tinyusb.h"

#ifndef BANNER_GIT
#define BANNER_GIT "unknown"
#endif

// E063 used 1209:0002. A different interface layout on the same PID would land
// on the same Windows device instance, so this experiment takes its own.
static constexpr uint16_t kTestVid = 0x1209;
static constexpr uint16_t kTestPid = 0x0003;

static constexpr size_t kPatternBytes = 16u * 1024u * 1024u;
static constexpr size_t kMaxChunk = 65536;
// USBCDC::write() times the whole call, so a large chunk needs a large budget.
static constexpr uint32_t kTxTimeoutMs = 10000;

static HWCDC Console;
static USBCDC HsCdc(0);

static uint8_t *pattern;
static bool host_armed;
static bool transfer_armed;
static size_t request_bytes;
static size_t request_chunk;
static char command[64];
static size_t command_length;

static void report_banner(void) {
  Console.printf("# EXP E064 v1 git=%s probe=esp32p4_usb target=none build=%s %s\n", BANNER_GIT, __DATE__, __TIME__);
}

static void report_env(void) {
  Console.printf(
    "ENV chip=%s rev=%u flash_size=%lu psram_found=%u psram_size=%lu pattern_bytes=%lu "
    "pattern_ready=%u cdc_tx_bufsize=%d mounted=%u speed=%d\n",
    ESP.getChipModel(), ESP.getChipRevision(), static_cast<unsigned long>(ESP.getFlashChipSize()),
    psramFound() ? 1U : 0U, static_cast<unsigned long>(ESP.getPsramSize()), static_cast<unsigned long>(kPatternBytes),
    pattern != nullptr ? 1U : 0U, CONFIG_TINYUSB_CDC_TX_BUFSIZE, tud_mounted() ? 1U : 0U,
    static_cast<int>(tud_speed_get())
  );
}

// word[i] = i, little-endian, so the host can verify every word and detect a
// gap by the misalignment it causes.
static bool fill_pattern(void) {
  pattern = static_cast<uint8_t *>(heap_caps_malloc(kPatternBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (pattern == nullptr) {
    return false;
  }
  uint32_t *words = reinterpret_cast<uint32_t *>(pattern);
  const size_t word_count = kPatternBytes / sizeof(uint32_t);
  for (size_t i = 0; i < word_count; ++i) {
    words[i] = static_cast<uint32_t>(i);
  }
  return true;
}

static void run_transfer(void) {
  const size_t total = request_bytes;
  const size_t chunk = request_chunk;
  size_t sent = 0;
  size_t written_total = 0;
  uint32_t short_writes = 0;

  const uint64_t started_us = esp_timer_get_time();
  while (sent < total) {
    const size_t want = (total - sent) < chunk ? (total - sent) : chunk;
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
  const uint64_t stopped_us = esp_timer_get_time();

  Console.printf(
    "SEND bytes=%lu chunk=%lu written=%lu short=%lu elapsed_us=%llu\n", static_cast<unsigned long>(total),
    static_cast<unsigned long>(chunk), static_cast<unsigned long>(written_total),
    static_cast<unsigned long>(short_writes), static_cast<unsigned long long>(stopped_us - started_us)
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
    unsigned long bytes = 0;
    unsigned long chunk = 0;
    if (sscanf(command + 1, "%lu %lu", &bytes, &chunk) != 2 || bytes == 0 || bytes > kPatternBytes || chunk == 0
        || chunk > kMaxChunk) {
      Console.printf("CFG status=reject bytes=%lu chunk=%lu\n", bytes, chunk);
      Console.flush();
      return;
    }
    request_bytes = bytes;
    request_chunk = chunk;
    transfer_armed = true;
    // Drop anything the previous round left behind, so 'G' is unambiguous.
    while (HsCdc.available()) {
      HsCdc.read();
    }
    Console.printf("CFG status=ok bytes=%lu chunk=%lu\n", bytes, chunk);
    Console.flush();
    return;
  }
  Console.printf("CFG status=unknown\n");
  Console.flush();
}

void setup() {
  Console.begin();

  const bool ready = fill_pattern();
  (void)ready;

  USB.VID(kTestVid);
  USB.PID(kTestPid);
  USB.manufacturerName("Open Embedded Probe (TEST ONLY)");
  USB.productName("OEP P4 HS CDC Rate Test");

  HsCdc.setTxTimeoutMs(kTxTimeoutMs);
  HsCdc.begin();
  USB.begin();
}

void loop() {
  if (transfer_armed && HsCdc.available() > 0) {
    const int trigger = HsCdc.read();
    if (trigger == 'G') {
      transfer_armed = false;
      run_transfer();
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
    Console.println("READY E064");
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
