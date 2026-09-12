// E067: what PARLIO capture and USB HS sending cost each other, and whether
// the core they are pinned to changes it.
// Plan and report: README.ja.md
//
// The capture path is the E021 spool shape (partial receive callbacks into a
// queue, a harvest task copying to PSRAM) narrowed to two lanes. The sender is
// the E066 shape. Both are pinned, and the modes move them between cores.

#include <Arduino.h>
#include <HWCDC.h>
#include <USB.h>
#include <USBCDC.h>
#include <driver/gpio.h>
#include <driver/parlio_rx.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "esp32-hal-tinyusb.h"

#ifndef BANNER_GIT
#define BANNER_GIT "unknown"
#endif
#ifndef PARLIO_PINS
#define PARLIO_PINS "2,3,4,5,6,7,8,9"
#endif

static constexpr uint16_t kTestVid = 0x1209;
static constexpr uint16_t kTestPid = 0x0006;

static constexpr size_t kLaneCount = 2;
static constexpr size_t kParsePins = 8;  // PARLIO_PINS always carries eight
static constexpr uint32_t kPwmFrequencyHz = 100000;
static constexpr uint8_t kPwmResolutionBits = 8;
static constexpr size_t kRingSize = 64 * 1024;
static constexpr size_t kDelimiterSize = 65408;
static constexpr size_t kQueueDepth = 64;
static constexpr size_t kSinkBytes = 1024 * 1024;
static constexpr size_t kPatternBytes = 8 * 1024 * 1024;
static constexpr size_t kUsbBytes = 4 * 1024 * 1024 - 100;  // not a multiple of 512
static constexpr size_t kUsbChunk = 4096;
static constexpr uint32_t kTxTimeoutMs = 10000;
static constexpr uint32_t kCaptureOnlyMs = 600;
static constexpr uint32_t kDrainRounds = 10;
static constexpr uint32_t kDrainStepMs = 10;
static constexpr uint32_t kDuties[kLaneCount] = {80, 176};

struct Mode {
  const char *name;
  bool capture;
  bool usb;
  BaseType_t harvest_core;
  BaseType_t usb_core;
};

// Baselines first, then the four ways of placing the two workers.
static const Mode kModes[] = {
  {"usb_c0", false, true, -1, 0},   {"usb_c1", false, true, -1, 1},
  {"cap_c0", true, false, 0, -1},   {"cap_c1", true, false, 1, -1},
  {"cap1_usb0", true, true, 1, 0},  {"cap0_usb1", true, true, 0, 1},
  {"cap0_usb0", true, true, 0, 0},  {"cap1_usb1", true, true, 1, 1},
};
static constexpr size_t kModeCount = sizeof(kModes) / sizeof(kModes[0]);

struct Chunk {
  const uint8_t *data;
  size_t length;
};

struct CaptureState {
  QueueHandle_t queue;
  volatile size_t callback_count;
  volatile size_t callback_bytes;
  volatile size_t queue_overflow;
};

static HWCDC Console;
static USBCDC HsCdc(0);

static int pins[kParsePins];
static bool ledc_attached[kLaneCount];
static uint8_t *ring_buffer;
static uint8_t *sink;
static uint8_t *pattern;
static CaptureState capture_state;

static volatile bool capture_stop;
static volatile size_t capture_copied;
static volatile uint64_t capture_elapsed_us;
static volatile bool capture_timed_out;
static SemaphoreHandle_t harvest_done;

static volatile uint64_t usb_elapsed_us;
static volatile size_t usb_written;
static volatile uint32_t usb_short;
static SemaphoreHandle_t usb_done;

static bool host_armed;
static bool transfer_armed;
static size_t request_mode;
static uint32_t request_rate_hz;
static char command[64];
static size_t command_length;

static bool parse_pins(void) {
  const char *cursor = PARLIO_PINS;
  for (size_t lane = 0; lane < kParsePins; ++lane) {
    char *end = nullptr;
    const long value = strtol(cursor, &end, 10);
    if (end == cursor || value < 0 || value >= GPIO_NUM_MAX) {
      return false;
    }
    pins[lane] = static_cast<int>(value);
    if (lane + 1 < kParsePins) {
      if (*end != ',') {
        return false;
      }
      cursor = end + 1;
    }
  }
  return true;
}

static esp_err_t configure_pwm(void) {
  for (size_t lane = 0; lane < kLaneCount; ++lane) {
    if (!ledcAttachChannel(pins[lane], kPwmFrequencyHz, kPwmResolutionBits, lane)) {
      return ESP_FAIL;
    }
    ledc_attached[lane] = true;
    if (!ledcWrite(pins[lane], kDuties[lane])) {
      return ESP_FAIL;
    }
  }
  return ESP_OK;
}

static void cleanup_pwm(void) {
  for (size_t lane = 0; lane < kLaneCount; ++lane) {
    if (ledc_attached[lane]) {
      ledcDetach(pins[lane]);
      ledc_attached[lane] = false;
    }
    gpio_reset_pin(static_cast<gpio_num_t>(pins[lane]));
  }
}

static bool IRAM_ATTR on_partial_receive(
  parlio_rx_unit_handle_t, const parlio_rx_event_data_t *event, void *user_data
) {
  auto *state = static_cast<CaptureState *>(user_data);
  const Chunk chunk = {static_cast<const uint8_t *>(event->data), event->recv_bytes};
  ++state->callback_count;
  state->callback_bytes += event->recv_bytes;
  BaseType_t high_task_woken = pdFALSE;
  if (xQueueSendFromISR(state->queue, &chunk, &high_task_woken) != pdTRUE) {
    ++state->queue_overflow;
  }
  return high_task_woken == pdTRUE;
}

static esp_err_t create_receiver(
  parlio_rx_unit_handle_t *rx_unit, parlio_rx_delimiter_handle_t *delimiter, uint32_t sample_rate_hz
) {
  parlio_rx_unit_config_t unit_config = {};
  unit_config.trans_queue_depth = 1;
  unit_config.max_recv_size = kRingSize;
  unit_config.dma_burst_size = 0;
  unit_config.data_width = kLaneCount;
  unit_config.clk_src = PARLIO_CLK_SRC_DEFAULT;
  unit_config.exp_clk_freq_hz = sample_rate_hz;
  unit_config.clk_in_gpio_num = GPIO_NUM_NC;
  unit_config.clk_out_gpio_num = GPIO_NUM_NC;
  unit_config.valid_gpio_num = GPIO_NUM_NC;
  for (size_t lane = 0; lane < PARLIO_RX_UNIT_MAX_DATA_WIDTH; ++lane) {
    unit_config.data_gpio_nums[lane] = lane < kLaneCount ? static_cast<gpio_num_t>(pins[lane]) : GPIO_NUM_NC;
  }
  unit_config.flags.io_loop_back = false;
  esp_err_t result = parlio_new_rx_unit(&unit_config, rx_unit);
  if (result != ESP_OK) {
    return result;
  }

  parlio_rx_event_callbacks_t callbacks = {};
  callbacks.on_partial_receive = on_partial_receive;
  result = parlio_rx_unit_register_event_callbacks(*rx_unit, &callbacks, &capture_state);
  if (result != ESP_OK) {
    return result;
  }

  parlio_rx_soft_delimiter_config_t delimiter_config = {};
  delimiter_config.sample_edge = PARLIO_SAMPLE_EDGE_POS;
  delimiter_config.bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB;
  delimiter_config.eof_data_len = kDelimiterSize;
  return parlio_new_rx_soft_delimiter(&delimiter_config, delimiter);
}

// The sink wraps, so the copy cost stays realistic without needing a buffer as
// large as the capture.
static void harvest_task(void *) {
  size_t copied = 0;
  size_t offset = 0;
  bool timed_out = false;
  const uint64_t started_us = esp_timer_get_time();
  while (!capture_stop) {
    Chunk chunk = {};
    if (xQueueReceive(capture_state.queue, &chunk, pdMS_TO_TICKS(200)) != pdTRUE) {
      timed_out = true;
      continue;
    }
    timed_out = false;
    size_t remaining = chunk.length;
    const uint8_t *source = chunk.data;
    while (remaining > 0) {
      const size_t room = kSinkBytes - offset;
      const size_t step = remaining < room ? remaining : room;
      memcpy(sink + offset, source, step);
      offset = (offset + step) % kSinkBytes;
      source += step;
      remaining -= step;
    }
    copied += chunk.length;
  }
  capture_elapsed_us = esp_timer_get_time() - started_us;
  capture_copied = copied;
  capture_timed_out = timed_out;
  xSemaphoreGive(harvest_done);
  vTaskDelete(nullptr);
}

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
  // The tail of a transfer can still be in the CDC FIFO here. Stay alive and
  // keep flushing for a moment before the run is declared over, so that the
  // measurement is not confused with the drain.
  for (uint32_t i = 0; i < kDrainRounds; ++i) {
    HsCdc.flush();
    vTaskDelay(pdMS_TO_TICKS(kDrainStepMs));
  }
  xSemaphoreGive(usb_done);
  vTaskDelete(nullptr);
}

static void report_banner(void) {
  Console.printf("# EXP E067 v1 git=%s probe=esp32p4_usb target=none build=%s %s\n", BANNER_GIT, __DATE__, __TIME__);
}

static void report_env(void) {
  Console.printf(
    "ENV chip=%s psram_found=%u pattern_ready=%u sink_ready=%u ring_ready=%u modes=%u lanes=%u "
    "usb_bytes=%lu ring_bytes=%lu arduino_core=%d mounted=%u speed=%d\n",
    ESP.getChipModel(), psramFound() ? 1U : 0U, pattern != nullptr ? 1U : 0U, sink != nullptr ? 1U : 0U,
    ring_buffer != nullptr ? 1U : 0U, kModeCount, kLaneCount, static_cast<unsigned long>(kUsbBytes),
    static_cast<unsigned long>(kRingSize), ARDUINO_RUNNING_CORE, tud_mounted() ? 1U : 0U,
    static_cast<int>(tud_speed_get())
  );
}

static void run_transfer(void) {
  const Mode &mode = kModes[request_mode];

  parlio_rx_unit_handle_t rx_unit = nullptr;
  parlio_rx_delimiter_handle_t delimiter = nullptr;
  esp_err_t capture_status = ESP_OK;
  bool harvest_started = false;

  usb_elapsed_us = 0;
  usb_written = 0;
  usb_short = 0;
  capture_stop = false;
  capture_copied = 0;
  capture_elapsed_us = 0;
  capture_timed_out = false;
  capture_state.callback_count = 0;
  capture_state.callback_bytes = 0;
  capture_state.queue_overflow = 0;
  xQueueReset(capture_state.queue);

  if (mode.capture) {
    memset(ring_buffer, 0xA5, kRingSize);
    esp_cache_msync(ring_buffer, kRingSize, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    capture_status = create_receiver(&rx_unit, &delimiter, request_rate_hz);
    if (capture_status == ESP_OK) {
      capture_status = configure_pwm();
    }
    if (capture_status == ESP_OK) {
      capture_status = parlio_rx_unit_enable(rx_unit, true);
    }
    if (capture_status == ESP_OK) {
      parlio_receive_config_t receive_config = {};
      receive_config.delimiter = delimiter;
      receive_config.flags.partial_rx_en = true;
      receive_config.flags.indirect_mount = false;
      capture_status = parlio_rx_unit_receive(rx_unit, ring_buffer, kRingSize, &receive_config);
    }
    if (capture_status == ESP_OK) {
      xTaskCreatePinnedToCore(harvest_task, "e067_harv", 4096, nullptr, 5, nullptr, mode.harvest_core);
      harvest_started = true;
      capture_status = parlio_rx_soft_delimiter_start_stop(rx_unit, delimiter, true);
    }
  }

  if (mode.usb) {
    xTaskCreatePinnedToCore(usb_task, "e067_tx", 4096, nullptr, 5, nullptr, mode.usb_core);
    xSemaphoreTake(usb_done, portMAX_DELAY);
  } else {
    delay(kCaptureOnlyMs);
  }

  if (mode.capture) {
    capture_stop = true;
    if (harvest_started) {
      xSemaphoreTake(harvest_done, portMAX_DELAY);
    }
    if (capture_status == ESP_OK) {
      parlio_rx_soft_delimiter_start_stop(rx_unit, delimiter, false);
    }
    parlio_rx_unit_disable(rx_unit);
    parlio_del_rx_delimiter(delimiter);
    parlio_del_rx_unit(rx_unit);
    cleanup_pwm();
  }

  Console.printf(
    "RUN mode=%u name=%s capture=%u usb=%u harvest_core=%d usb_core=%d rate_hz=%lu status=%d "
    "usb_elapsed_us=%llu usb_written=%lu usb_short=%lu "
    "cap_elapsed_us=%llu cap_callback_bytes=%lu cap_copied=%lu cap_overflow=%lu cap_timeout=%u\n",
    request_mode, mode.name, mode.capture ? 1U : 0U, mode.usb ? 1U : 0U, static_cast<int>(mode.harvest_core),
    static_cast<int>(mode.usb_core), static_cast<unsigned long>(request_rate_hz), static_cast<int>(capture_status),
    static_cast<unsigned long long>(usb_elapsed_us), static_cast<unsigned long>(usb_written),
    static_cast<unsigned long>(usb_short), static_cast<unsigned long long>(capture_elapsed_us),
    static_cast<unsigned long>(capture_state.callback_bytes), static_cast<unsigned long>(capture_copied),
    static_cast<unsigned long>(capture_state.queue_overflow), capture_timed_out ? 1U : 0U
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
    unsigned long mode = 0;
    unsigned long rate = 0;
    if (sscanf(command + 1, "%lu %lu", &mode, &rate) != 2 || mode >= kModeCount || rate == 0) {
      Console.printf("CFG status=reject mode=%lu rate_hz=%lu\n", mode, rate);
      Console.flush();
      return;
    }
    request_mode = mode;
    request_rate_hz = static_cast<uint32_t>(rate);
    while (HsCdc.available()) {
      HsCdc.read();
    }
    // Capture-only modes have no reader to send 'G', so they start here.
    transfer_armed = kModes[mode].usb;
    Console.printf("CFG status=ok mode=%lu rate_hz=%lu needs_go=%u\n", mode, rate, transfer_armed ? 1U : 0U);
    Console.flush();
    if (!transfer_armed) {
      run_transfer();
    }
    return;
  }
  Console.printf("CFG status=unknown\n");
  Console.flush();
}

void setup() {
  Console.begin();

  parse_pins();
  capture_state.queue = xQueueCreate(kQueueDepth, sizeof(Chunk));
  harvest_done = xSemaphoreCreateBinary();
  usb_done = xSemaphoreCreateBinary();

  ring_buffer = static_cast<uint8_t *>(heap_caps_aligned_alloc(64, kRingSize, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  sink = static_cast<uint8_t *>(heap_caps_malloc(kSinkBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  pattern = static_cast<uint8_t *>(heap_caps_malloc(kPatternBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (pattern != nullptr) {
    uint32_t *words = reinterpret_cast<uint32_t *>(pattern);
    for (size_t i = 0; i < kPatternBytes / sizeof(uint32_t); ++i) {
      words[i] = static_cast<uint32_t>(i);
    }
  }

  USB.VID(kTestVid);
  USB.PID(kTestPid);
  USB.manufacturerName("Open Embedded Probe (TEST ONLY)");
  USB.productName("OEP P4 USB vs Capture Test");

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
    Console.println("READY E067");
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
