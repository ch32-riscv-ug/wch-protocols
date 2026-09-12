// E065: does a second CDC on the OTG HS port raise the combined rate.
// Plan and report: README.ja.md
//
// One sender task per CDC port, pinned to a different core, so that the two
// endpoints wait on their own turnaround rather than on each other. Both tasks
// start together: the device holds them until every active port has seen 'G'.

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

// E063 used 0002 and E064 used 0003; a new interface layout gets its own PID so
// that Windows keeps the device instances apart.
static constexpr uint16_t kTestVid = 0x1209;
static constexpr uint16_t kTestPid = 0x0004;

static constexpr size_t kPatternBytes = 16u * 1024u * 1024u;
static constexpr size_t kMaxChunk = 65536;
static constexpr uint32_t kTxTimeoutMs = 10000;
static constexpr uint8_t kPortCount = 2;

static HWCDC Console;
static USBCDC HsCdc[kPortCount] = {USBCDC(0), USBCDC(1)};

static uint8_t *pattern;
static bool host_armed;
static bool transfer_armed;
static uint8_t active_ports;
static size_t request_bytes;
static size_t request_chunk;
static bool port_triggered[kPortCount];

static SemaphoreHandle_t start_signal[kPortCount];
static SemaphoreHandle_t done_signal[kPortCount];
static volatile uint64_t port_elapsed_us[kPortCount];
static volatile size_t port_written[kPortCount];
static volatile uint32_t port_short[kPortCount];

static char command[64];
static size_t command_length;

static void sender_task(void *argument) {
  const uint8_t index = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(argument));
  for (;;) {
    xSemaphoreTake(start_signal[index], portMAX_DELAY);

    const size_t total = request_bytes;
    const size_t chunk = request_chunk;
    size_t sent = 0;
    size_t written_total = 0;
    uint32_t short_writes = 0;

    const uint64_t started_us = esp_timer_get_time();
    while (sent < total) {
      const size_t want = (total - sent) < chunk ? (total - sent) : chunk;
      const size_t written = HsCdc[index].write(pattern + sent, want);
      written_total += written;
      if (written != want) {
        ++short_writes;
        if (written == 0) {
          break;
        }
      }
      sent += written;
    }
    HsCdc[index].flush();
    port_elapsed_us[index] = esp_timer_get_time() - started_us;
    port_written[index] = written_total;
    port_short[index] = short_writes;

    xSemaphoreGive(done_signal[index]);
  }
}

static void report_banner(void) {
  Console.printf("# EXP E065 v1 git=%s probe=esp32p4_usb target=none build=%s %s\n", BANNER_GIT, __DATE__, __TIME__);
}

static void report_env(void) {
  Console.printf(
    "ENV chip=%s psram_found=%u psram_size=%lu pattern_bytes=%lu pattern_ready=%u cdc_ports=%d "
    "cdc_tx_bufsize=%d mounted=%u speed=%d\n",
    ESP.getChipModel(), psramFound() ? 1U : 0U, static_cast<unsigned long>(ESP.getPsramSize()),
    static_cast<unsigned long>(kPatternBytes), pattern != nullptr ? 1U : 0U, CFG_TUD_CDC, CONFIG_TINYUSB_CDC_TX_BUFSIZE,
    tud_mounted() ? 1U : 0U, static_cast<int>(tud_speed_get())
  );
}

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

// Every active port has seen 'G', so the senders can start together and the
// span really covers the overlap.
static void run_transfer(void) {
  const uint64_t started_us = esp_timer_get_time();
  for (uint8_t index = 0; index < active_ports; ++index) {
    xSemaphoreGive(start_signal[index]);
  }
  for (uint8_t index = 0; index < active_ports; ++index) {
    xSemaphoreTake(done_signal[index], portMAX_DELAY);
  }
  const uint64_t span_us = esp_timer_get_time() - started_us;

  Console.printf(
    "SEND ports=%u bytes=%lu chunk=%lu span_us=%llu", active_ports, static_cast<unsigned long>(request_bytes),
    static_cast<unsigned long>(request_chunk), static_cast<unsigned long long>(span_us)
  );
  for (uint8_t index = 0; index < active_ports; ++index) {
    Console.printf(
      " p%u_us=%llu p%u_written=%lu p%u_short=%lu", index, static_cast<unsigned long long>(port_elapsed_us[index]),
      index, static_cast<unsigned long>(port_written[index]), index, static_cast<unsigned long>(port_short[index])
    );
  }
  Console.println();
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
    unsigned long ports = 0;
    if (sscanf(command + 1, "%lu %lu %lu", &bytes, &chunk, &ports) != 3 || bytes == 0 || bytes > kPatternBytes
        || chunk == 0 || chunk > kMaxChunk || ports == 0 || ports > kPortCount) {
      Console.printf("CFG status=reject bytes=%lu chunk=%lu ports=%lu\n", bytes, chunk, ports);
      Console.flush();
      return;
    }
    request_bytes = bytes;
    request_chunk = chunk;
    active_ports = static_cast<uint8_t>(ports);
    for (uint8_t index = 0; index < kPortCount; ++index) {
      port_triggered[index] = false;
      while (HsCdc[index].available()) {
        HsCdc[index].read();
      }
    }
    transfer_armed = true;
    Console.printf("CFG status=ok bytes=%lu chunk=%lu ports=%lu\n", bytes, chunk, ports);
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
  USB.productName("OEP P4 HS Dual CDC Rate Test");

  for (uint8_t index = 0; index < kPortCount; ++index) {
    start_signal[index] = xSemaphoreCreateBinary();
    done_signal[index] = xSemaphoreCreateBinary();
    HsCdc[index].setTxTimeoutMs(kTxTimeoutMs);
    HsCdc[index].begin();
  }
  USB.begin();

  for (uint8_t index = 0; index < kPortCount; ++index) {
    char name[16];
    snprintf(name, sizeof(name), "e065_tx%u", index);
    xTaskCreatePinnedToCore(
      sender_task, name, 4096, reinterpret_cast<void *>(static_cast<uintptr_t>(index)), 5, nullptr, index
    );
  }
}

void loop() {
  if (transfer_armed) {
    uint8_t ready_count = 0;
    for (uint8_t index = 0; index < active_ports; ++index) {
      if (!port_triggered[index] && HsCdc[index].available() > 0) {
        if (HsCdc[index].read() == 'G') {
          port_triggered[index] = true;
        }
      }
      if (port_triggered[index]) {
        ++ready_count;
      }
    }
    if (ready_count == active_ports) {
      transfer_armed = false;
      run_transfer();
    } else {
      delay(1);
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
    Console.println("READY E065");
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
