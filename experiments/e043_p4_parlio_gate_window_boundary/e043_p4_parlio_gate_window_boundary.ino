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

#define EXPERIMENT_ID "E043"

namespace {

constexpr size_t kSourceLaneCount = 8;
constexpr size_t kDataWidth = 4;
constexpr size_t kValidSourceBit = 4;
constexpr uint32_t kValidLineId = 4;
constexpr uint32_t kSampleRateHz = 20000000;
constexpr uint32_t kSourceDivider = 4;
constexpr size_t kSourceWords = 16384;
constexpr size_t kGateIndex = 2048;
constexpr size_t kRingSize = 64 * 1024;
constexpr size_t kDestinationSize = 256 * 1024;
constexpr size_t kQueueDepth = 64;
constexpr int64_t kHarvestUs = 50000;
constexpr size_t kReportedChunks = 24;
constexpr size_t kReportedViolations = 8;
// Gate widths deliberately off the 16-word gray period, so a window boundary
// shows up as a step that is not +1.
constexpr size_t kGateWidths[] = {2044, 1020};

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

int pins[kSourceLaneCount] = {};
uint8_t *ring_buffer = nullptr;
uint8_t *destination = nullptr;
uint8_t *source_pattern = nullptr;
CaptureState capture_state = {};
size_t chunk_lengths[kReportedChunks] = {};
size_t violation_positions[kReportedViolations] = {};

uint8_t to_gray4(uint8_t index) {
  return static_cast<uint8_t>((index ^ (index >> 1)) & 0x0F);
}

uint8_t from_gray4(uint8_t value) {
  value = static_cast<uint8_t>(value & 0x0F);
  value ^= value >> 1;
  value ^= value >> 2;
  return static_cast<uint8_t>(value & 0x0F);
}

bool parse_pins() {
  const char *cursor = PARLIO_PINS;
  for (size_t lane = 0; lane < kSourceLaneCount; ++lane) {
    char *end = nullptr;
    const long value = strtol(cursor, &end, 10);
    if (end == cursor || value < 0 || value >= GPIO_NUM_MAX) return false;
    pins[lane] = static_cast<int>(value);
    if (lane + 1 < kSourceLaneCount) {
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

void build_pattern(size_t gate_words) {
  for (size_t index = 0; index < kSourceWords; ++index) {
    uint8_t word = to_gray4(static_cast<uint8_t>(index & 0x0F));
    if (index >= kGateIndex && index < kGateIndex + gate_words) {
      word |= static_cast<uint8_t>(1u << kValidSourceBit);
    }
    source_pattern[index] = word;
  }
  esp_cache_msync(source_pattern, kSourceWords, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

esp_err_t create_receiver(parlio_rx_unit_handle_t *rx_unit) {
  parlio_rx_unit_config_t unit_config = {};
  unit_config.trans_queue_depth = 1;
  unit_config.max_recv_size = kRingSize;
  unit_config.data_width = kDataWidth;
  unit_config.clk_src = PARLIO_CLK_SRC_DEFAULT;
  unit_config.exp_clk_freq_hz = kSampleRateHz;
  unit_config.clk_in_gpio_num = GPIO_NUM_NC;
  unit_config.clk_out_gpio_num = GPIO_NUM_NC;
  unit_config.valid_gpio_num = static_cast<gpio_num_t>(pins[kValidSourceBit]);
  for (size_t lane = 0; lane < PARLIO_RX_UNIT_MAX_DATA_WIDTH; ++lane) {
    unit_config.data_gpio_nums[lane] =
        lane < kDataWidth ? static_cast<gpio_num_t>(pins[lane]) : GPIO_NUM_NC;
  }
  unit_config.flags.io_loop_back = false;
  esp_err_t result = parlio_new_rx_unit(&unit_config, rx_unit);
  if (result != ESP_OK) return result;

  parlio_rx_event_callbacks_t callbacks = {};
  callbacks.on_partial_receive = on_partial_receive;
  return parlio_rx_unit_register_event_callbacks(*rx_unit, &callbacks,
                                                 &capture_state);
}

esp_err_t create_source(parlio_tx_unit_handle_t *tx_unit) {
  parlio_tx_unit_config_t unit_config = {};
  unit_config.clk_src = PARLIO_CLK_SRC_DEFAULT;
  unit_config.clk_in_gpio_num = GPIO_NUM_NC;
  unit_config.input_clk_src_freq_hz = 0;
  unit_config.output_clk_freq_hz = kSampleRateHz / kSourceDivider;
  unit_config.data_width = kSourceLaneCount;
  for (size_t lane = 0; lane < PARLIO_TX_UNIT_MAX_DATA_WIDTH; ++lane) {
    unit_config.data_gpio_nums[lane] =
        lane < kSourceLaneCount ? static_cast<gpio_num_t>(pins[lane])
                                : GPIO_NUM_NC;
  }
  unit_config.clk_out_gpio_num = GPIO_NUM_NC;
  unit_config.valid_gpio_num = GPIO_NUM_NC;
  unit_config.valid_start_delay = 0;
  unit_config.valid_stop_delay = 0;
  unit_config.trans_queue_depth = 1;
  unit_config.max_transfer_size = kSourceWords;
  unit_config.dma_burst_size = 0;
  unit_config.sample_edge = PARLIO_SAMPLE_EDGE_NEG;
  unit_config.bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB;
  unit_config.flags.clk_gate_en = false;
  unit_config.flags.io_loop_back = false;
  unit_config.flags.allow_pd = false;
  unit_config.flags.invert_valid_out = false;
  return parlio_new_tx_unit(&unit_config, tx_unit);
}

uint8_t unpack_nibble(const uint8_t *data, size_t sample) {
  const size_t bit_offset = sample * kDataWidth;
  return static_cast<uint8_t>((data[bit_offset / 8] >> (bit_offset % 8)) & 0x0F);
}

void run_case(size_t gate_words) {
  const size_t expected_window =
      gate_words * kSourceDivider * kDataWidth / 8;
  xQueueReset(capture_state.queue);
  capture_state.callback_count = 0;
  capture_state.callback_bytes = 0;
  capture_state.queue_overflow = 0;
  memset(chunk_lengths, 0, sizeof(chunk_lengths));
  memset(violation_positions, 0, sizeof(violation_positions));
  memset(ring_buffer, 0xA5, kRingSize);
  memset(destination, 0xA5, kDestinationSize);
  esp_cache_msync(ring_buffer, kRingSize, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  esp_err_t sync_result = esp_cache_msync(destination, kDestinationSize,
                                          ESP_CACHE_MSYNC_FLAG_DIR_C2M);

  parlio_rx_unit_handle_t rx_unit = nullptr;
  parlio_rx_delimiter_handle_t delimiter = nullptr;
  parlio_tx_unit_handle_t tx_unit = nullptr;

  const esp_err_t config_result = create_receiver(&rx_unit);

  esp_err_t delimiter_result = ESP_FAIL;
  if (config_result == ESP_OK) {
    parlio_rx_level_delimiter_config_t delimiter_config = {};
    delimiter_config.valid_sig_line_id = kValidLineId;
    delimiter_config.sample_edge = PARLIO_SAMPLE_EDGE_POS;
    delimiter_config.bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB;
    delimiter_config.eof_data_len = 0;
    delimiter_config.timeout_ticks = 0;
    delimiter_config.flags.active_low_en = false;
    delimiter_result =
        parlio_new_rx_level_delimiter(&delimiter_config, &delimiter);
  }

  esp_err_t source_result = ESP_FAIL;
  esp_err_t source_start_result = ESP_FAIL;
  if (delimiter_result == ESP_OK) {
    build_pattern(gate_words);
    source_result = create_source(&tx_unit);
  }
  if (source_result == ESP_OK) source_start_result = parlio_tx_unit_enable(tx_unit);
  if (source_start_result == ESP_OK) {
    parlio_transmit_config_t transmit_config = {};
    transmit_config.idle_value = 0;
    transmit_config.bitscrambler_program = nullptr;
    transmit_config.flags.queue_nonblocking = false;
    transmit_config.flags.loop_transmission = true;
    source_start_result = parlio_tx_unit_transmit(
        tx_unit, source_pattern, kSourceWords * 8, &transmit_config);
  }

  esp_err_t enable_result = ESP_FAIL;
  if (source_start_result == ESP_OK) {
    enable_result = parlio_rx_unit_enable(rx_unit, true);
  }

  esp_err_t receive_result = ESP_FAIL;
  if (enable_result == ESP_OK && sync_result == ESP_OK) {
    parlio_receive_config_t receive_config = {};
    receive_config.delimiter = delimiter;
    receive_config.flags.partial_rx_en = true;
    receive_config.flags.indirect_mount = false;
    receive_result = parlio_rx_unit_receive(rx_unit, ring_buffer, kRingSize,
                                            &receive_config);
  }

  size_t harvested = 0;
  size_t dequeues = 0;
  int64_t harvest_us = 0;
  if (receive_result == ESP_OK) {
    const int64_t harvest_begin = esp_timer_get_time();
    while (esp_timer_get_time() - harvest_begin < kHarvestUs) {
      Chunk chunk = {};
      if (xQueueReceive(capture_state.queue, &chunk, pdMS_TO_TICKS(5)) !=
          pdTRUE) {
        continue;
      }
      if (dequeues < kReportedChunks) chunk_lengths[dequeues] = chunk.length;
      const size_t copy_size = min(chunk.length, kDestinationSize - harvested);
      if (copy_size > 0) {
        memcpy(destination + harvested, chunk.data, copy_size);
        harvested += copy_size;
      }
      ++dequeues;
    }
    harvest_us = esp_timer_get_time() - harvest_begin;
  }

  const esp_err_t disable_result =
      rx_unit != nullptr ? parlio_rx_unit_disable(rx_unit) : ESP_FAIL;
  if (harvested > 0) {
    sync_result = esp_cache_msync(destination, kDestinationSize,
                                  ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (sync_result == ESP_OK) {
      sync_result = esp_cache_msync(destination, kDestinationSize,
                                    ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    }
  }

  size_t runs = 0;
  size_t violations = 0;
  size_t run_min = 0;
  size_t run_max = 0;
  size_t aligned_violations = 0;
  if (harvested > 1 && sync_result == ESP_OK) {
    const size_t samples = harvested * 8 / kDataWidth;
    size_t smallest = SIZE_MAX;
    uint8_t previous = unpack_nibble(destination, 0);
    size_t run = 1;
    for (size_t sample = 1; sample < samples; ++sample) {
      const uint8_t value = unpack_nibble(destination, sample);
      if (value == previous) {
        ++run;
        continue;
      }
      if (runs != 0) {
        smallest = min(smallest, run);
        run_max = max(run_max, run);
      }
      if (static_cast<uint8_t>((from_gray4(value) - from_gray4(previous)) &
                               0x0F) != 1) {
        const size_t offset = sample * kDataWidth / 8;
        if (violations < kReportedViolations) {
          violation_positions[violations] = offset;
        }
        if (expected_window > 0 && offset % expected_window == 0) {
          ++aligned_violations;
        }
        ++violations;
      }
      ++runs;
      run = 1;
      previous = value;
    }
    run_min = smallest == SIZE_MAX ? 0 : smallest;
  }

  const uint32_t harvest_kbps =
      harvest_us > 0
          ? static_cast<uint32_t>(harvested * 1000ULL / harvest_us)
          : 0;

  Serial.printf(
      "CASE gate_words=%lu expected_window=%lu config=%s delimiter=%s "
      "source=%s source_start=%s enable=%s receive=%s disable=%s sync=%s "
      "callbacks=%lu dequeues=%lu harvested=%lu overflow=%lu runs=%lu "
      "run_min=%lu run_max=%lu violations=%lu aligned_violations=%lu "
      "harvest_us=%lld harvest_kbps=%lu\n",
      static_cast<unsigned long>(gate_words),
      static_cast<unsigned long>(expected_window),
      esp_err_to_name(config_result), esp_err_to_name(delimiter_result),
      esp_err_to_name(source_result), esp_err_to_name(source_start_result),
      esp_err_to_name(enable_result), esp_err_to_name(receive_result),
      esp_err_to_name(disable_result), esp_err_to_name(sync_result),
      static_cast<unsigned long>(capture_state.callback_count),
      static_cast<unsigned long>(dequeues),
      static_cast<unsigned long>(harvested),
      static_cast<unsigned long>(capture_state.queue_overflow),
      static_cast<unsigned long>(runs), static_cast<unsigned long>(run_min),
      static_cast<unsigned long>(run_max),
      static_cast<unsigned long>(violations),
      static_cast<unsigned long>(aligned_violations), harvest_us,
      static_cast<unsigned long>(harvest_kbps));

  Serial.printf("CHUNKS gate_words=%lu", static_cast<unsigned long>(gate_words));
  for (size_t index = 0; index < kReportedChunks; ++index) {
    Serial.printf(" %lu", static_cast<unsigned long>(chunk_lengths[index]));
  }
  Serial.println();

  Serial.printf("VIOLS gate_words=%lu", static_cast<unsigned long>(gate_words));
  for (size_t index = 0; index < kReportedViolations; ++index) {
    Serial.printf(" %lu",
                  static_cast<unsigned long>(violation_positions[index]));
  }
  Serial.println();

  if (tx_unit != nullptr) {
    parlio_tx_unit_disable(tx_unit);
    parlio_del_tx_unit(tx_unit);
  }
  if (delimiter != nullptr) parlio_del_rx_delimiter(delimiter);
  if (rx_unit != nullptr) parlio_del_rx_unit(rx_unit);
  for (size_t lane = 0; lane < kSourceLaneCount; ++lane) {
    gpio_reset_pin(static_cast<gpio_num_t>(pins[lane]));
  }
}

void run_experiment() {
  Serial.print("# EXP " EXPERIMENT_ID " v1 git=");
  Serial.print(BANNER_GIT);
  Serial.print(" probe=esp32p4_parlio target=internal build=");
  Serial.println(__DATE__ " " __TIME__);
  Serial.printf(
      "ENV parlio_groups=%u rx_units=%u data_width=%u valid_pin=%d "
      "sample_rate_hz=%lu source_words=%lu gate_index=%lu reported_chunks=%u\n",
      static_cast<unsigned>(SOC_PARLIO_GROUPS),
      static_cast<unsigned>(SOC_PARLIO_RX_UNITS_PER_GROUP),
      static_cast<unsigned>(kDataWidth), pins[kValidSourceBit],
      static_cast<unsigned long>(kSampleRateHz),
      static_cast<unsigned long>(kSourceWords),
      static_cast<unsigned long>(kGateIndex),
      static_cast<unsigned>(kReportedChunks));

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
      internal_alignment, kSourceWords,
      MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  destination = static_cast<uint8_t *>(heap_caps_aligned_alloc(
      external_alignment, kDestinationSize,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  capture_state.queue = xQueueCreate(kQueueDepth, sizeof(Chunk));
  Serial.printf("BUFFERS ring_ok=%u source_ok=%u destination_ok=%u queue_ok=%u\n",
                static_cast<unsigned>(ring_buffer != nullptr &&
                                      esp_ptr_dma_capable(ring_buffer)),
                static_cast<unsigned>(source_pattern != nullptr &&
                                      esp_ptr_dma_capable(source_pattern)),
                static_cast<unsigned>(destination != nullptr &&
                                      esp_ptr_external_ram(destination)),
                static_cast<unsigned>(capture_state.queue != nullptr));
  if (ring_buffer == nullptr || source_pattern == nullptr ||
      destination == nullptr || capture_state.queue == nullptr) {
    Serial.println("DONE status=alloc-failed");
    return;
  }

  for (size_t gate_words : kGateWidths) run_case(gate_words);

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
