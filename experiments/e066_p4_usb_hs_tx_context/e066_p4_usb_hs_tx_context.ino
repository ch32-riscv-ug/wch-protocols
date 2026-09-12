// E066: does the sender's execution context set the CDC download rate.
// Plan and report: README.ja.md
//
// One CDC only, so the descriptor matches E064 and the second interface that
// E065 carried is not a variable here. The transfer size and chunk are fixed;
// the only thing that moves is where the bytes are written from.

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
static constexpr uint16_t kTestPid = 0x0005;

static constexpr size_t kPatternBytes = 16u * 1024u * 1024u;
static constexpr size_t kMaxChunk = 65536;
static constexpr uint32_t kTxTimeoutMs = 10000;

struct SenderContext {
  const char *name;
  bool use_loop;
  UBaseType_t priority;
  BaseType_t core;  // tskNO_AFFINITY when unpinned
};

// Priority 20 stays below the usbd task (configMAX_PRIORITIES - 1), so raising
// the sender never starves the task that services the endpoint.
static const SenderContext kContexts[] = {
  {"loop", true, 1, ARDUINO_RUNNING_CORE},
  {"task_p1_c0", false, 1, 0},
  {"task_p5_c0", false, 5, 0},
  {"task_p5_c1", false, 5, 1},
  {"task_p20_c0", false, 20, 0},
  {"task_p5_free", false, 5, tskNO_AFFINITY},
};
static constexpr size_t kContextCount = sizeof(kContexts) / sizeof(kContexts[0]);

static HWCDC Console;
static USBCDC HsCdc(0);

static uint8_t *pattern;
static bool host_armed;
static bool transfer_armed;
static size_t request_bytes;
static size_t request_chunk;
static size_t request_mode;

static SemaphoreHandle_t done_signal;
static volatile uint64_t run_elapsed_us;
static volatile size_t run_written;
static volatile uint32_t run_short;
static volatile int run_core;

static char command[64];
static size_t command_length;

static void send_pattern(void) {
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
  run_elapsed_us = esp_timer_get_time() - started_us;
  run_written = written_total;
  run_short = short_writes;
  run_core = xPortGetCoreID();
}

static void sender_task(void *) {
  send_pattern();
  xSemaphoreGive(done_signal);
  vTaskDelete(nullptr);
}

static void report_banner(void) {
  Console.printf("# EXP E066 v1 git=%s probe=esp32p4_usb target=none build=%s %s\n", BANNER_GIT, __DATE__, __TIME__);
}

static void report_env(void) {
  Console.printf(
    "ENV chip=%s psram_found=%u pattern_ready=%u modes=%u max_priorities=%d arduino_core=%d "
    "cdc_tx_bufsize=%d mounted=%u speed=%d\n",
    ESP.getChipModel(), psramFound() ? 1U : 0U, pattern != nullptr ? 1U : 0U, kContextCount, configMAX_PRIORITIES,
    ARDUINO_RUNNING_CORE, CONFIG_TINYUSB_CDC_TX_BUFSIZE, tud_mounted() ? 1U : 0U, static_cast<int>(tud_speed_get())
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

static void run_transfer(void) {
  const SenderContext &context = kContexts[request_mode];
  if (context.use_loop) {
    send_pattern();
  } else {
    xTaskCreatePinnedToCore(sender_task, "e066_tx", 4096, nullptr, context.priority, nullptr, context.core);
    xSemaphoreTake(done_signal, portMAX_DELAY);
  }

  Console.printf(
    "SEND mode=%u name=%s priority=%u core=%d ran_on=%d bytes=%lu chunk=%lu written=%lu short=%lu elapsed_us=%llu\n",
    request_mode, context.name, static_cast<unsigned>(context.priority), static_cast<int>(context.core), run_core,
    static_cast<unsigned long>(request_bytes), static_cast<unsigned long>(request_chunk),
    static_cast<unsigned long>(run_written), static_cast<unsigned long>(run_short),
    static_cast<unsigned long long>(run_elapsed_us)
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
    unsigned long mode = 0;
    if (sscanf(command + 1, "%lu %lu %lu", &bytes, &chunk, &mode) != 3 || bytes == 0 || bytes > kPatternBytes
        || chunk == 0 || chunk > kMaxChunk || mode >= kContextCount) {
      Console.printf("CFG status=reject bytes=%lu chunk=%lu mode=%lu\n", bytes, chunk, mode);
      Console.flush();
      return;
    }
    request_bytes = bytes;
    request_chunk = chunk;
    request_mode = mode;
    while (HsCdc.available()) {
      HsCdc.read();
    }
    transfer_armed = true;
    Console.printf("CFG status=ok bytes=%lu chunk=%lu mode=%lu\n", bytes, chunk, mode);
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

  done_signal = xSemaphoreCreateBinary();

  USB.VID(kTestVid);
  USB.PID(kTestPid);
  USB.manufacturerName("Open Embedded Probe (TEST ONLY)");
  USB.productName("OEP P4 HS TX Context Test");

  HsCdc.setTxTimeoutMs(kTxTimeoutMs);
  HsCdc.begin();
  USB.begin();
}

void loop() {
  if (transfer_armed && HsCdc.available() > 0) {
    if (HsCdc.read() == 'G') {
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
    Console.println("READY E066");
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
