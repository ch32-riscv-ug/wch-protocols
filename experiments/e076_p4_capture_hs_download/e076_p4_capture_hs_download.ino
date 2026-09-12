// E076: capture two channels with PARLIO into PSRAM, then hand the packed bytes
// to the host so it can write a sigrok .sr file.
// Plan and report: README.ja.md
//
// The capture path is the E074 shape: PARLIO RX into an internal DMA ring,
// partial-receive callbacks into a queue, copied to PSRAM. What changes here is
// the download -- it leaves over the OTG HS vendor bulk endpoint instead of the
// full-speed console, while the console keeps carrying the commands so the two
// paths stay separate.

#include <Arduino.h>
#include <HWCDC.h>

#include "EspUsbDevice.h"
#include <driver/gpio.h>
#include <driver/parlio_rx.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#ifndef BANNER_GIT
#define BANNER_GIT "unknown"
#endif
#ifndef PARLIO_PINS
#define PARLIO_PINS "2,3,4,5,6,7,8,9"
#endif

static constexpr size_t kLaneCount = 2;          // 2 channels -> 4 samples per byte
static constexpr size_t kParsePins = 8;
static constexpr uint32_t kPwmFrequencyHz = 100000;
static constexpr uint8_t kPwmResolutionBits = 8;
static constexpr uint32_t kDuties[kLaneCount] = {64, 128};  // 25% and 50%
static constexpr size_t kRingSize = 64 * 1024;
static constexpr size_t kDelimiterSize = 65408;
static constexpr size_t kQueueDepth = 64;
static constexpr size_t kCaptureBytesMax = 4u * 1024u * 1024u;
static constexpr size_t kControlBytes = 128u * 1024u;  // internal RAM, sent repeatedly

static constexpr uint16_t kTestVid = 0x1209;
static constexpr uint16_t kTestPid = 0x0008;

static HWCDC Console;
static EspUsbDevice device;
static EspUsbDeviceVendor HsVendor(device, 512);
static bool usb_ready;
static constexpr size_t kUsbChunk = 4096;
static constexpr BaseType_t kSenderCore = 0;  // E066: the core, not the priority

static SemaphoreHandle_t usb_done;
static const uint8_t *tx_base;
static size_t tx_span;
static size_t tx_total;
static volatile uint64_t usb_elapsed_us;
static volatile size_t usb_written;
static volatile uint32_t usb_stalls;

struct Chunk {
  const uint8_t *data;
  size_t length;
};

struct CaptureState {
  QueueHandle_t queue;
  volatile size_t callback_bytes;
  volatile size_t queue_overflow;
};

static int pins[kParsePins];
static bool ledc_attached[kLaneCount];
static uint8_t *ring_buffer;
static uint8_t *capture;
static uint8_t *control_internal;
static CaptureState capture_state;

static size_t capture_bytes;
static uint32_t capture_rate_hz;
static uint64_t capture_elapsed_us;
static bool host_armed;
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

static void run_capture(size_t want_bytes, uint32_t rate_hz) {
  capture_bytes = 0;
  capture_rate_hz = rate_hz;
  capture_state.callback_bytes = 0;
  capture_state.queue_overflow = 0;
  xQueueReset(capture_state.queue);
  memset(ring_buffer, 0xA5, kRingSize);
  esp_cache_msync(ring_buffer, kRingSize, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

  parlio_rx_unit_handle_t rx_unit = nullptr;
  parlio_rx_delimiter_handle_t delimiter = nullptr;
  esp_err_t status = create_receiver(&rx_unit, &delimiter, rate_hz);
  if (status == ESP_OK) {
    status = configure_pwm();
  }
  if (status == ESP_OK) {
    status = parlio_rx_unit_enable(rx_unit, true);
  }
  if (status == ESP_OK) {
    parlio_receive_config_t receive_config = {};
    receive_config.delimiter = delimiter;
    receive_config.flags.partial_rx_en = true;
    receive_config.flags.indirect_mount = false;
    status = parlio_rx_unit_receive(rx_unit, ring_buffer, kRingSize, &receive_config);
  }

  size_t copied = 0;
  bool timed_out = false;
  const uint64_t started = esp_timer_get_time();
  if (status == ESP_OK) {
    status = parlio_rx_soft_delimiter_start_stop(rx_unit, delimiter, true);
  }
  while (status == ESP_OK && copied < want_bytes) {
    Chunk chunk = {};
    if (xQueueReceive(capture_state.queue, &chunk, pdMS_TO_TICKS(500)) != pdTRUE) {
      timed_out = true;
      break;
    }
    const size_t step = (want_bytes - copied) < chunk.length ? (want_bytes - copied) : chunk.length;
    memcpy(capture + copied, chunk.data, step);
    copied += step;
  }
  capture_elapsed_us = esp_timer_get_time() - started;
  capture_bytes = copied;

  if (rx_unit) {
    parlio_rx_soft_delimiter_start_stop(rx_unit, delimiter, false);
    parlio_rx_unit_disable(rx_unit);
  }
  if (delimiter) {
    parlio_del_rx_delimiter(delimiter);
  }
  if (rx_unit) {
    parlio_del_rx_unit(rx_unit);
  }
  cleanup_pwm();

  Console.printf(
    "CAP status=%d rate_hz=%lu bytes=%lu samples=%lu overflow=%lu timeout=%u elapsed_us=%llu\n",
    static_cast<int>(status), static_cast<unsigned long>(rate_hz), static_cast<unsigned long>(capture_bytes),
    static_cast<unsigned long>(capture_bytes * 4), static_cast<unsigned long>(capture_state.queue_overflow),
    timed_out ? 1U : 0U, static_cast<unsigned long long>(capture_elapsed_us)
  );
  Console.flush();
}

// The capture leaves over the OTG HS vendor endpoint. Bulk IN is host-polled,
// so starting before the host reads loses nothing: write() simply returns 0.
// The source is `tx_base` and wraps at `tx_span`, which lets the same loop send
// either the capture (span = the whole capture) or a smaller buffer repeatedly.
static void usb_task(void *) {
  size_t sent = 0;
  uint32_t stalls = 0;
  size_t offset = 0;
  const uint64_t started = esp_timer_get_time();
  while (sent < tx_total) {
    const size_t room = tx_span - offset;
    size_t want = (tx_total - sent) < room ? (tx_total - sent) : room;
    if (want > kUsbChunk) {
      want = kUsbChunk;
    }
    const size_t written = HsVendor.write(tx_base + offset, want);
    if (written == 0) {
      ++stalls;
      HsVendor.flush();
      taskYIELD();
      continue;
    }
    sent += written;
    offset = (offset + written) % tx_span;
  }
  HsVendor.flush();
  usb_elapsed_us = esp_timer_get_time() - started;
  usb_written = sent;
  usb_stalls = stalls;
  xSemaphoreGive(usb_done);
  vTaskDelete(nullptr);
}

static void send_over_hs(const uint8_t *base, size_t span, size_t total) {
  usb_elapsed_us = 0;
  usb_written = 0;
  usb_stalls = 0;
  tx_base = base;
  tx_span = span;
  tx_total = total;
  xTaskCreatePinnedToCore(usb_task, "e076_tx", 4096, nullptr, 5, nullptr, kSenderCore);
  xSemaphoreTake(usb_done, portMAX_DELAY);
  Console.printf("SENT bytes=%lu stalls=%lu elapsed_us=%llu\n", static_cast<unsigned long>(usb_written),
                 static_cast<unsigned long>(usb_stalls), static_cast<unsigned long long>(usb_elapsed_us));
  Console.flush();
}

static void run_dump(void) {
  Console.printf("DUMP bytes=%lu\n", static_cast<unsigned long>(capture_bytes));
  Console.flush();
  send_over_hs(capture, capture_bytes ? capture_bytes : 1, capture_bytes);
}

// The control for "is PSRAM the reason the download is slower than E069/E071":
// the same loop, the same endpoint, but the source is internal RAM.
static void run_bandwidth(size_t total, bool from_psram) {
  const uint8_t *base = from_psram ? capture : control_internal;
  if (base == nullptr) {
    Console.println("DUMP status=nobuf");
    Console.flush();
    return;
  }
  const size_t span = from_psram ? kCaptureBytesMax : kControlBytes;
  Console.printf("DUMP bytes=%lu source=%s\n", static_cast<unsigned long>(total), from_psram ? "psram" : "internal");
  Console.flush();
  send_over_hs(base, span, total);
}

static void handle_command(void) {
  if (command[0] == '?') {
    Console.printf("# EXP E076 v1 git=%s probe=esp32p4_parlio target=internal build=%s %s\n", BANNER_GIT, __DATE__,
                   __TIME__);
    Console.printf(
      "ENV chip=%s psram_found=%u psram_size=%lu lanes=%u pins=%d,%d pwm_hz=%lu duties=%lu,%lu capture_max=%lu "
      "ring=%lu control=%lu usb_ready=%u mounted=%u ready=%u\n",
      ESP.getChipModel(), psramFound() ? 1U : 0U, static_cast<unsigned long>(ESP.getPsramSize()), kLaneCount, pins[0],
      pins[1], static_cast<unsigned long>(kPwmFrequencyHz), static_cast<unsigned long>(kDuties[0]),
      static_cast<unsigned long>(kDuties[1]), static_cast<unsigned long>(kCaptureBytesMax),
      static_cast<unsigned long>(kRingSize), static_cast<unsigned long>(control_internal ? kControlBytes : 0),
      usb_ready ? 1U : 0U, HsVendor.mounted() ? 1U : 0U, (capture && ring_buffer) ? 1U : 0U
    );
    Console.flush();
    return;
  }
  if (command[0] == 'C') {
    unsigned long bytes = 0;
    unsigned long rate = 0;
    if (sscanf(command + 1, "%lu %lu", &bytes, &rate) != 2 || bytes == 0 || bytes > kCaptureBytesMax || rate == 0) {
      Console.printf("CAP status=reject bytes=%lu rate_hz=%lu\n", bytes, rate);
      Console.flush();
      return;
    }
    run_capture(bytes, static_cast<uint32_t>(rate));
    return;
  }
  if (command[0] == 'D') {
    run_dump();
    return;
  }
  if (command[0] == 'B') {
    unsigned long bytes = 0;
    unsigned long source = 0;
    if (sscanf(command + 1, "%lu %lu", &bytes, &source) != 2 || bytes == 0) {
      Console.println("DUMP status=reject");
      Console.flush();
      return;
    }
    run_bandwidth(bytes, source != 0);
    return;
  }
  Console.println("CFG status=unknown");
  Console.flush();
}

void setup() {
  Console.begin();
  parse_pins();
  capture_state.queue = xQueueCreate(kQueueDepth, sizeof(Chunk));
  ring_buffer = static_cast<uint8_t *>(heap_caps_aligned_alloc(64, kRingSize, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  capture = static_cast<uint8_t *>(heap_caps_malloc(kCaptureBytesMax, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  control_internal = static_cast<uint8_t *>(heap_caps_malloc(kControlBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (control_internal) {
    memset(control_internal, 0x5A, kControlBytes);
  }
  usb_done = xSemaphoreCreateBinary();

  EspUsbDeviceConfig config;
  config.vid = kTestVid;
  config.pid = kTestPid;
  config.manufacturer = "Open Embedded Probe (TEST ONLY)";
  config.product = "OEP P4 Capture Download";
  // Same identity as E069/E071 so usbipd's existing bind still applies.
  config.serialNumber = "E069-A";
  config.selfPowered = true;
  config.maxPowerMilliamps = 500;
  config.webusbEnabled = true;
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
    Console.println("READY E076");
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
