#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/parlio_rx.h>
#include <driver/parlio_tx.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <esp_private/esp_cache_private.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <soc/soc_caps.h>

#ifndef BANNER_GIT
#define BANNER_GIT "unknown"
#endif

#ifndef PARLIO_PINS
#define PARLIO_PINS "2,3,4,5,6,7,8,9"
#endif

#define EXPERIMENT_ID "E036"

namespace {

constexpr size_t kLaneCount = 8;
constexpr size_t kSampleCount = 1024 * 1024;
constexpr size_t kRingSize = 64 * 1024;
// Keep the delimiter, ring and queue identical to E031 so the driver splits
// the transaction into the same 4,032-byte descriptors and the numbers stay
// comparable with E032 / E033.
constexpr size_t kDelimiterSize = 65408;
constexpr size_t kQueueDepth = 64;
// 32 gray periods. 8192 % 256 == 0, so the loop seam is also a +1 step.
constexpr size_t kSourceSize = 8192;
constexpr uint32_t kSourceDivider = 4;
constexpr uint32_t kRates[] = {
    20000000, 80000000, 96000000, 100000000, 104000000, 112000000, 120000000,
};

struct Chunk {
  const uint8_t *data;
  size_t length;
};

// The ISR only ever increments these, and the task only ever reads them, so
// the in-flight byte count needs no lock.
struct CaptureState {
  QueueHandle_t queue;
  volatile size_t callback_count;
  volatile size_t callback_bytes;
  volatile size_t queue_overflow;
};

struct SeqStats {
  size_t runs;
  size_t violations;
  size_t first_violation;
  size_t run_min;
  size_t run_max;
};

int pins[kLaneCount] = {};
uint8_t *ring_buffer = nullptr;
uint8_t *destination = nullptr;
uint8_t *source_pattern = nullptr;
CaptureState capture_state = {};

uint8_t to_gray(uint8_t index) { return index ^ (index >> 1); }

uint8_t from_gray(uint8_t value) {
  value ^= value >> 1;
  value ^= value >> 2;
  value ^= value >> 4;
  return value;
}

bool parse_pins() {
  const char *cursor = PARLIO_PINS;
  for (size_t lane = 0; lane < kLaneCount; ++lane) {
    char *end = nullptr;
    const long value = strtol(cursor, &end, 10);
    if (end == cursor || value < 0 || value >= GPIO_NUM_MAX) return false;
    pins[lane] = static_cast<int>(value);
    if (lane + 1 < kLaneCount) {
      if (*end != ',') return false;
      cursor = end + 1;
    } else if (*end != '\0') {
      return false;
    }
  }
  return true;
}

bool IRAM_ATTR on_partial_receive(parlio_rx_unit_handle_t,
                                  const parlio_rx_event_data_t *event,
                                  void *user_data) {
  auto *state = static_cast<CaptureState *>(user_data);
  const Chunk chunk = {
      .data = static_cast<const uint8_t *>(event->data),
      .length = event->recv_bytes,
  };
  ++state->callback_count;
  state->callback_bytes += event->recv_bytes;
  BaseType_t high_task_woken = pdFALSE;
  if (xQueueSendFromISR(state->queue, &chunk, &high_task_woken) != pdTRUE) {
    ++state->queue_overflow;
  }
  return high_task_woken == pdTRUE;
}

esp_err_t create_receiver(uint32_t sample_rate_hz,
                          parlio_rx_unit_handle_t *rx_unit,
                          parlio_rx_delimiter_handle_t *delimiter) {
  parlio_rx_unit_config_t unit_config = {};
  unit_config.trans_queue_depth = 1;
  unit_config.max_recv_size = kRingSize;
  unit_config.data_width = kLaneCount;
  unit_config.clk_src = PARLIO_CLK_SRC_DEFAULT;
  unit_config.exp_clk_freq_hz = sample_rate_hz;
  unit_config.clk_in_gpio_num = GPIO_NUM_NC;
  unit_config.clk_out_gpio_num = GPIO_NUM_NC;
  unit_config.valid_gpio_num = GPIO_NUM_NC;
  for (size_t lane = 0; lane < PARLIO_RX_UNIT_MAX_DATA_WIDTH; ++lane) {
    unit_config.data_gpio_nums[lane] =
        lane < kLaneCount ? static_cast<gpio_num_t>(pins[lane]) : GPIO_NUM_NC;
  }
  unit_config.flags.io_loop_back = false;
  esp_err_t result = parlio_new_rx_unit(&unit_config, rx_unit);
  if (result != ESP_OK) return result;

  parlio_rx_event_callbacks_t callbacks = {};
  callbacks.on_partial_receive = on_partial_receive;
  result = parlio_rx_unit_register_event_callbacks(*rx_unit, &callbacks,
                                                   &capture_state);
  if (result != ESP_OK) return result;

  parlio_rx_soft_delimiter_config_t delimiter_config = {};
  delimiter_config.sample_edge = PARLIO_SAMPLE_EDGE_POS;
  delimiter_config.bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB;
  delimiter_config.eof_data_len = kDelimiterSize;
  return parlio_new_rx_soft_delimiter(&delimiter_config, delimiter);
}

// E015 established that the input path can be added without disturbing an
// existing output path as long as io_loop_back stays false, and E031 creates
// the receiver before the source. Keep that order here.
esp_err_t create_source(uint32_t sample_rate_hz,
                        parlio_tx_unit_handle_t *tx_unit) {
  parlio_tx_unit_config_t unit_config = {};
  unit_config.clk_src = PARLIO_CLK_SRC_DEFAULT;
  unit_config.clk_in_gpio_num = GPIO_NUM_NC;
  unit_config.input_clk_src_freq_hz = 0;
  unit_config.output_clk_freq_hz = sample_rate_hz / kSourceDivider;
  unit_config.data_width = kLaneCount;
  for (size_t lane = 0; lane < PARLIO_TX_UNIT_MAX_DATA_WIDTH; ++lane) {
    unit_config.data_gpio_nums[lane] =
        lane < kLaneCount ? static_cast<gpio_num_t>(pins[lane]) : GPIO_NUM_NC;
  }
  unit_config.clk_out_gpio_num = GPIO_NUM_NC;
  unit_config.valid_gpio_num = GPIO_NUM_NC;
  unit_config.valid_start_delay = 0;
  unit_config.valid_stop_delay = 0;
  unit_config.trans_queue_depth = 1;
  unit_config.max_transfer_size = kSourceSize;
  unit_config.dma_burst_size = 0;
  unit_config.sample_edge = PARLIO_SAMPLE_EDGE_NEG;
  unit_config.bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB;
  unit_config.flags.clk_gate_en = false;
  unit_config.flags.io_loop_back = false;
  unit_config.flags.allow_pd = false;
  unit_config.flags.invert_valid_out = false;
  return parlio_new_tx_unit(&unit_config, tx_unit);
}

SeqStats verify_sequence(const uint8_t *data, size_t count) {
  SeqStats stats = {0, 0, 0, SIZE_MAX, 0};
  uint8_t previous = data[0];
  size_t run = 1;
  for (size_t index = 1; index < count; ++index) {
    const uint8_t value = data[index];
    if (value == previous) {
      ++run;
      continue;
    }
    // The run that started before sample 0 is truncated, so it says nothing
    // about the source rate.
    if (stats.runs != 0) {
      stats.run_min = min(stats.run_min, run);
      stats.run_max = max(stats.run_max, run);
    }
    if (static_cast<uint8_t>(from_gray(value) - from_gray(previous)) != 1) {
      ++stats.violations;
      if (stats.first_violation == 0) stats.first_violation = index;
    }
    ++stats.runs;
    run = 1;
    previous = value;
  }
  if (stats.run_min == SIZE_MAX) stats.run_min = 0;
  return stats;
}

void run_case(uint32_t sample_rate_hz) {
  xQueueReset(capture_state.queue);
  capture_state.callback_count = 0;
  capture_state.callback_bytes = 0;
  capture_state.queue_overflow = 0;
  memset(ring_buffer, 0xA5, kRingSize);
  memset(destination, 0xA5, kSampleCount);
  esp_cache_msync(ring_buffer, kRingSize, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  esp_err_t sync_result =
      esp_cache_msync(destination, kSampleCount, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

  parlio_rx_unit_handle_t rx_unit = nullptr;
  parlio_rx_delimiter_handle_t delimiter = nullptr;
  parlio_tx_unit_handle_t tx_unit = nullptr;
  const esp_err_t config_result =
      create_receiver(sample_rate_hz, &rx_unit, &delimiter);
  esp_err_t source_result = ESP_FAIL;
  esp_err_t source_start_result = ESP_FAIL;
  if (config_result == ESP_OK) {
    source_result = create_source(sample_rate_hz, &tx_unit);
  }
  if (source_result == ESP_OK) {
    source_start_result = parlio_tx_unit_enable(tx_unit);
  }
  if (source_start_result == ESP_OK) {
    parlio_transmit_config_t transmit_config = {};
    transmit_config.idle_value = 0;
    transmit_config.bitscrambler_program = nullptr;
    transmit_config.flags.queue_nonblocking = false;
    transmit_config.flags.loop_transmission = true;
    source_start_result = parlio_tx_unit_transmit(
        tx_unit, source_pattern, kSourceSize * 8, &transmit_config);
  }

  esp_err_t enable_result = ESP_FAIL;
  if (source_start_result == ESP_OK) {
    enable_result = parlio_rx_unit_enable(rx_unit, true);
  }

  parlio_receive_config_t receive_config = {};
  receive_config.delimiter = delimiter;
  receive_config.flags.partial_rx_en = true;
  receive_config.flags.indirect_mount = false;
  esp_err_t receive_result = ESP_FAIL;
  if (enable_result == ESP_OK && sync_result == ESP_OK) {
    receive_result = parlio_rx_unit_receive(rx_unit, ring_buffer, kRingSize,
                                           &receive_config);
  }

  esp_err_t start_result = ESP_FAIL;
  size_t copied = 0;
  size_t consumed = 0;
  size_t dequeue_count = 0;
  size_t max_queue_depth = 0;
  size_t max_inflight = 0;
  size_t sampled_bytes = 0;
  bool timed_out = false;
  const int64_t capture_begin = esp_timer_get_time();
  if (receive_result == ESP_OK) {
    start_result = parlio_rx_soft_delimiter_start_stop(rx_unit, delimiter, true);
  }
  while (start_result == ESP_OK && copied < kSampleCount) {
    Chunk chunk = {};
    if (xQueueReceive(capture_state.queue, &chunk, pdMS_TO_TICKS(1000)) !=
        pdTRUE) {
      timed_out = true;
      break;
    }
    const size_t copy_size = min(chunk.length, kSampleCount - copied);
    memcpy(destination + copied, chunk.data, copy_size);
    copied += copy_size;
    consumed += chunk.length;
    ++dequeue_count;
    max_queue_depth = max(max_queue_depth, static_cast<size_t>(
                                               uxQueueMessagesWaiting(
                                                   capture_state.queue)));
    // Bytes the DMA has already written into the ring but the task has not
    // taken out yet. Above kRingSize the ring has wrapped over unread data.
    sampled_bytes = capture_state.callback_bytes;
    max_inflight = max(max_inflight,
                       sampled_bytes > consumed ? sampled_bytes - consumed : 0);
  }
  const int64_t capture_us = esp_timer_get_time() - capture_begin;

  esp_err_t stop_result = ESP_FAIL;
  if (start_result == ESP_OK) {
    stop_result = parlio_rx_soft_delimiter_start_stop(rx_unit, delimiter, false);
  }
  const esp_err_t disable_result =
      rx_unit != nullptr ? parlio_rx_unit_disable(rx_unit) : ESP_FAIL;
  if (!timed_out && disable_result == ESP_OK) {
    sync_result = esp_cache_msync(destination, kSampleCount,
                                  ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (sync_result == ESP_OK) {
      sync_result = esp_cache_msync(destination, kSampleCount,
                                    ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    }
  } else if (timed_out) {
    sync_result = ESP_ERR_TIMEOUT;
  }

  esp_err_t result = config_result;
  if (result == ESP_OK) result = source_result;
  if (result == ESP_OK) result = source_start_result;
  if (result == ESP_OK) result = enable_result;
  if (result == ESP_OK) result = receive_result;
  if (result == ESP_OK) result = start_result;
  if (result == ESP_OK && timed_out) result = ESP_ERR_TIMEOUT;
  if (result == ESP_OK) result = stop_result;
  if (result == ESP_OK) result = disable_result;
  if (result == ESP_OK) result = sync_result;

  SeqStats stats = {0, 0, 0, 0, 0};
  if (copied == kSampleCount && !timed_out) {
    stats = verify_sequence(destination, kSampleCount);
  }

  const uint64_t sampling_kbps =
      capture_us > 0 ? sampled_bytes * 1000ULL / capture_us : 0;
  const uint64_t spool_kbps =
      capture_us > 0 ? copied * 1000ULL / capture_us : 0;

  Serial.printf(
      "CASE rx_rate_hz=%lu tx_rate_hz=%lu result=%s config=%s source=%s "
      "source_start=%s enable=%s receive=%s start=%s stop=%s disable=%s "
      "sync=%s target_bytes=%lu copied=%lu callbacks=%lu dequeues=%lu "
      "extra_bytes=%lu overflows=%lu max_queue=%lu inflight_max=%lu "
      "ring_bytes=%lu ring_overrun=%u runs=%lu run_min=%lu run_max=%lu "
      "violations=%lu first_violation=%lu capture_us=%lld "
      "sampling_kbps=%llu spool_kbps=%llu\n",
      static_cast<unsigned long>(sample_rate_hz),
      static_cast<unsigned long>(sample_rate_hz / kSourceDivider),
      esp_err_to_name(result), esp_err_to_name(config_result),
      esp_err_to_name(source_result), esp_err_to_name(source_start_result),
      esp_err_to_name(enable_result), esp_err_to_name(receive_result),
      esp_err_to_name(start_result), esp_err_to_name(stop_result),
      esp_err_to_name(disable_result), esp_err_to_name(sync_result),
      static_cast<unsigned long>(kSampleCount),
      static_cast<unsigned long>(copied),
      static_cast<unsigned long>(capture_state.callback_count),
      static_cast<unsigned long>(dequeue_count),
      static_cast<unsigned long>(capture_state.callback_bytes > copied
                                     ? capture_state.callback_bytes - copied
                                     : 0),
      static_cast<unsigned long>(capture_state.queue_overflow),
      static_cast<unsigned long>(max_queue_depth),
      static_cast<unsigned long>(max_inflight),
      static_cast<unsigned long>(kRingSize),
      static_cast<unsigned>(max_inflight > kRingSize ? 1 : 0),
      static_cast<unsigned long>(stats.runs),
      static_cast<unsigned long>(stats.run_min),
      static_cast<unsigned long>(stats.run_max),
      static_cast<unsigned long>(stats.violations),
      static_cast<unsigned long>(stats.first_violation), capture_us,
      sampling_kbps, spool_kbps);

  if (tx_unit != nullptr) {
    parlio_tx_unit_disable(tx_unit);
    parlio_del_tx_unit(tx_unit);
  }
  if (delimiter != nullptr) parlio_del_rx_delimiter(delimiter);
  if (rx_unit != nullptr) parlio_del_rx_unit(rx_unit);
  for (size_t lane = 0; lane < kLaneCount; ++lane) {
    gpio_reset_pin(static_cast<gpio_num_t>(pins[lane]));
  }
}

void run_experiment() {
  Serial.print("# EXP " EXPERIMENT_ID " v1 git=");
  Serial.print(BANNER_GIT);
  Serial.print(" probe=esp32p4_parlio target=internal build=");
  Serial.println(__DATE__ " " __TIME__);
  Serial.printf(
      "ENV psram_found=%u psram_size=%lu parlio_groups=%u tx_units=%u "
      "rx_units=%u max_tx_width=%u max_rx_width=%u\n",
      static_cast<unsigned>(psramFound()),
      static_cast<unsigned long>(ESP.getPsramSize()),
      static_cast<unsigned>(SOC_PARLIO_GROUPS),
      static_cast<unsigned>(SOC_PARLIO_TX_UNITS_PER_GROUP),
      static_cast<unsigned>(SOC_PARLIO_RX_UNITS_PER_GROUP),
      static_cast<unsigned>(PARLIO_TX_UNIT_MAX_DATA_WIDTH),
      static_cast<unsigned>(PARLIO_RX_UNIT_MAX_DATA_WIDTH));
  if (!psramFound()) {
    Serial.println("DONE status=environment-failed");
    return;
  }

  size_t internal_alignment = 0;
  size_t external_alignment = 0;
  if (esp_cache_get_alignment(MALLOC_CAP_INTERNAL, &internal_alignment) !=
          ESP_OK ||
      esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &external_alignment) !=
          ESP_OK) {
    Serial.println("DONE status=alignment-failed");
    return;
  }
  ring_buffer = static_cast<uint8_t *>(heap_caps_aligned_alloc(
      internal_alignment, kRingSize,
      MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  source_pattern = static_cast<uint8_t *>(heap_caps_aligned_alloc(
      internal_alignment, kSourceSize,
      MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  destination = static_cast<uint8_t *>(heap_caps_aligned_alloc(
      external_alignment, kSampleCount, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  capture_state.queue = xQueueCreate(kQueueDepth, sizeof(Chunk));
  Serial.printf("BUFFERS ring_ok=%u source_ok=%u destination_ok=%u queue_ok=%u\n",
                static_cast<unsigned>(ring_buffer != nullptr &&
                                      esp_ptr_internal(ring_buffer) &&
                                      esp_ptr_dma_capable(ring_buffer)),
                static_cast<unsigned>(source_pattern != nullptr &&
                                      esp_ptr_internal(source_pattern) &&
                                      esp_ptr_dma_capable(source_pattern)),
                static_cast<unsigned>(destination != nullptr &&
                                      esp_ptr_external_ram(destination)),
                static_cast<unsigned>(capture_state.queue != nullptr));
  if (ring_buffer == nullptr || source_pattern == nullptr ||
      destination == nullptr || capture_state.queue == nullptr) {
    Serial.println("DONE status=alloc-failed");
    return;
  }

  for (size_t index = 0; index < kSourceSize; ++index) {
    source_pattern[index] = to_gray(static_cast<uint8_t>(index));
  }
  esp_cache_msync(source_pattern, kSourceSize, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

  for (uint32_t rate : kRates) run_case(rate);

  vQueueDelete(capture_state.queue);
  free(ring_buffer);
  free(source_pattern);
  free(destination);
  Serial.println("DONE status=ok");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  if (!parse_pins()) {
    Serial.println("DONE status=pin-config-failed");
  }
}

void loop() {
  if (Serial.available() <= 0) {
    delay(1);
    return;
  }
  while (Serial.available() > 0) Serial.read();
  run_experiment();
}
