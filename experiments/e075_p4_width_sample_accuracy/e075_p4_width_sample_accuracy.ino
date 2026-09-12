// E075: capture two channels with PARLIO into PSRAM, then hand the packed bytes
// to the host so it can write a sigrok .sr file.
// Plan and report: README.ja.md
//
// The capture path is the E067 shape narrowed to the console: PARLIO RX into an
// internal DMA ring, partial-receive callbacks into a queue, a harvest task
// copying to PSRAM. The download goes over the USB-Serial-JTAG console rather
// than the OTG HS port, because the HS port is cabled to the second board.

#include <Arduino.h>
#include <HWCDC.h>
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

#ifndef LANE_COUNT
#define LANE_COUNT 2
#endif
static constexpr size_t kLaneCount = LANE_COUNT;  // 8 / kLaneCount samples per byte
static constexpr size_t kParsePins = 8;
static constexpr uint32_t kPwmFrequencyHz = 100000;
static constexpr uint8_t kPwmResolutionBits = 8;
// Distinct duties per lane so the host can tell the channels apart.
static constexpr uint32_t kAllDuties[8] = {32, 64, 96, 128, 160, 176, 192, 208};
static constexpr size_t kRingSize = 64 * 1024;
static constexpr size_t kDelimiterSize = 65408;
static constexpr size_t kQueueDepth = 64;
static constexpr size_t kCaptureBytesMax = 4u * 1024u * 1024u;

static HWCDC Console;

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
    if (!ledcWrite(pins[lane], kAllDuties[lane])) {
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
    "CAP status=%d lanes=%u rate_hz=%lu bytes=%lu samples=%lu overflow=%lu timeout=%u elapsed_us=%llu\n",
    static_cast<int>(status), (unsigned)kLaneCount, static_cast<unsigned long>(rate_hz), static_cast<unsigned long>(capture_bytes),
    static_cast<unsigned long>(capture_bytes * (8 / kLaneCount)), static_cast<unsigned long>(capture_state.queue_overflow),
    timed_out ? 1U : 0U, static_cast<unsigned long long>(capture_elapsed_us)
  );
  Console.flush();
}

// Raw bytes follow the DUMP line with nothing else interleaved, so the host can
// read exactly the count it was told.
static void run_dump(void) {
  Console.printf("DUMP bytes=%lu\n", static_cast<unsigned long>(capture_bytes));
  Console.flush();
  size_t sent = 0;
  while (sent < capture_bytes) {
    const size_t step = (capture_bytes - sent) < 4096 ? (capture_bytes - sent) : 4096;
    const size_t written = Console.write(capture + sent, step);
    if (written == 0) {
      delay(1);
      continue;
    }
    sent += written;
  }
  Console.flush();
}

static void handle_command(void) {
  if (command[0] == '?') {
    Console.printf("# EXP E075 v1 git=%s probe=esp32p4_parlio target=internal build=%s %s\n", BANNER_GIT, __DATE__,
                   __TIME__);
    Console.printf(
      "ENV chip=%s psram_found=%u psram_size=%lu lanes=%u pins=%d,%d pwm_hz=%lu duties=%lu,%lu capture_max=%lu "
      "ring=%lu samples_per_byte=%u ready=%u\n",
      ESP.getChipModel(), psramFound() ? 1U : 0U, static_cast<unsigned long>(ESP.getPsramSize()), kLaneCount, pins[0],
      pins[1], static_cast<unsigned long>(kPwmFrequencyHz), static_cast<unsigned long>(kAllDuties[0]),
      static_cast<unsigned long>(kAllDuties[1]), static_cast<unsigned long>(kCaptureBytesMax),
      static_cast<unsigned long>(kRingSize), (unsigned)(8 / kLaneCount), (capture && ring_buffer) ? 1U : 0U
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
  Console.println("CFG status=unknown");
  Console.flush();
}

void setup() {
  Console.begin();
  parse_pins();
  capture_state.queue = xQueueCreate(kQueueDepth, sizeof(Chunk));
  ring_buffer = static_cast<uint8_t *>(heap_caps_aligned_alloc(64, kRingSize, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  capture = static_cast<uint8_t *>(heap_caps_malloc(kCaptureBytesMax, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
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
    Console.println("READY E075");
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
