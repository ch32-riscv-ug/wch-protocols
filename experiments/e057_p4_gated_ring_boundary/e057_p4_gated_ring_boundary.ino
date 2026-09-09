#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/parlio_rx.h>
#include <driver/parlio_tx.h>
#include <driver/rmt_rx.h>
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

#define EXPERIMENT_ID "E057"

namespace {

constexpr size_t kSourceLaneCount = 8;
constexpr size_t kDataWidth = 8;
// The valid line, data line 7 and the RMT input all share this pin.
constexpr size_t kSharedBit = 7;
constexpr uint32_t kValidLineId = 8;
// The source stays put so RMT's tick counts are invariant; only the PARLIO
// sample rate moves, and window sample counts scale with it.
constexpr uint32_t kSourceRateHz = 5000000;
constexpr uint32_t kSampleRateHz = 160000000;
// The gap tracks the window, so the duty is 50% in every case and only the
// window length moves. 7,000 words is the largest whose RMT duration stays
// under signal_range_max_ns (E049 hit that limit at 8,000).
constexpr size_t kGateWidths[] = {1000, 2000, 3000, 4000, 5000};
// E049's unread-to-window ratio put the in-window drain near 82 MB/s, which
// is what sets the peak backlog. The 98 MB/s sustained figure applies to the
// mean rate (condition 1), not to a single window.
constexpr uint32_t kSpoolMBps = 82;
// Shorter than E043's loop so the gate-low span stays inside the 15-bit
// duration field of an RMT symbol.
constexpr size_t kSourceWords = 16384;  // pattern buffer capacity

// 15.5 times the 4,096-byte sample pattern period, so a ring overwrite shows
// up instead of aliasing away (E056).
constexpr size_t kRingSize = 63488;
constexpr size_t kDestinationSize = 4 * 1024 * 1024;
constexpr size_t kQueueDepth = 64;
constexpr int64_t kHarvestUs = 100000;
constexpr size_t kReportedViolations = 16;

// 1 RMT tick == 1 PARLIO sample, so durations read directly as sample counts.
constexpr uint32_t kRmtResolutionHz = 20000000;
constexpr size_t kRmtMemBlockSymbols = 48;
constexpr size_t kRmtBufferSymbols = 32;
constexpr uint32_t kRmtMinNs = 500;
constexpr uint32_t kRmtMaxNs = 1600000;
constexpr size_t kReportedSymbols = 16;

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

struct RmtState {
  volatile size_t callback_count;
  volatile size_t stored;
  uint32_t durations[kReportedSymbols * 2];
  uint8_t levels[kReportedSymbols * 2];
};

int pins[kSourceLaneCount] = {};
uint8_t *ring_buffer = nullptr;
uint8_t *destination = nullptr;
uint8_t *source_pattern = nullptr;
rmt_symbol_word_t *rmt_buffer = nullptr;
CaptureState capture_state = {};
RmtState rmt_state = {};

uint8_t to_gray7(uint8_t index) {
  index = static_cast<uint8_t>(index & 0x7F);
  return static_cast<uint8_t>((index ^ (index >> 1)) & 0x7F);
}

uint8_t from_gray7(uint8_t value) {
  value = static_cast<uint8_t>(value & 0x7F);
  value ^= value >> 1;
  value ^= value >> 2;
  value ^= value >> 4;
  return static_cast<uint8_t>(value & 0x7F);
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

bool IRAM_ATTR on_rmt_recv_done(rmt_channel_handle_t,
                                const rmt_rx_done_event_data_t *event,
                                void *user_data) {
  auto *state = static_cast<RmtState *>(user_data);
  ++state->callback_count;
  for (size_t index = 0; index < event->num_symbols; ++index) {
    const rmt_symbol_word_t symbol = event->received_symbols[index];
    if (state->stored < kReportedSymbols * 2) {
      state->durations[state->stored] = symbol.duration0;
      state->levels[state->stored] = static_cast<uint8_t>(symbol.level0);
      ++state->stored;
    }
    if (state->stored < kReportedSymbols * 2) {
      state->durations[state->stored] = symbol.duration1;
      state->levels[state->stored] = static_cast<uint8_t>(symbol.level1);
      ++state->stored;
    }
  }
  return false;
}

// The window sits at the head of the loop so the captured ramp restarts at 0
// each window; the loop seam falls inside the gap and is never sampled.
void build_pattern(size_t gate_words, size_t loop_words) {
  for (size_t index = 0; index < loop_words; ++index) {
    uint8_t word = to_gray7(static_cast<uint8_t>(index & 0x7F));
    if (index < gate_words) {
      word |= static_cast<uint8_t>(1u << kSharedBit);
    }
    source_pattern[index] = word;
  }
  // Sync the whole buffer: loop_words is not a multiple of the 64-byte
  // cache line, which E051 found makes esp_cache_msync fail.
  esp_cache_msync(source_pattern, kSourceWords, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

esp_err_t create_receiver(uint32_t sample_rate_hz,
                          parlio_rx_unit_handle_t *rx_unit) {
  parlio_rx_unit_config_t unit_config = {};
  unit_config.trans_queue_depth = 1;
  unit_config.max_recv_size = kRingSize;
  unit_config.data_width = kDataWidth;
  unit_config.clk_src = PARLIO_CLK_SRC_DEFAULT;
  unit_config.exp_clk_freq_hz = sample_rate_hz;
  unit_config.clk_in_gpio_num = GPIO_NUM_NC;
  unit_config.clk_out_gpio_num = GPIO_NUM_NC;
  unit_config.valid_gpio_num = static_cast<gpio_num_t>(pins[kSharedBit]);
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

// The same GPIO already feeds PARLIO's valid input; the GPIO matrix can fan
// one input out to several peripheral input signals (E041).
esp_err_t create_rmt(rmt_channel_handle_t *rx_channel) {
  rmt_rx_channel_config_t channel_config = {};
  channel_config.gpio_num = static_cast<gpio_num_t>(pins[kSharedBit]);
  channel_config.clk_src = RMT_CLK_SRC_DEFAULT;
  channel_config.resolution_hz = kRmtResolutionHz;
  channel_config.mem_block_symbols = kRmtMemBlockSymbols;
  channel_config.intr_priority = 0;
  channel_config.flags.invert_in = false;
  channel_config.flags.with_dma = false;
  channel_config.flags.io_loop_back = false;
  channel_config.flags.allow_pd = false;
  esp_err_t result = rmt_new_rx_channel(&channel_config, rx_channel);
  if (result != ESP_OK) return result;

  rmt_rx_event_callbacks_t callbacks = {};
  callbacks.on_recv_done = on_rmt_recv_done;
  return rmt_rx_register_event_callbacks(*rx_channel, &callbacks, &rmt_state);
}

esp_err_t create_source(parlio_tx_unit_handle_t *tx_unit) {
  parlio_tx_unit_config_t unit_config = {};
  unit_config.clk_src = PARLIO_CLK_SRC_DEFAULT;
  unit_config.clk_in_gpio_num = GPIO_NUM_NC;
  unit_config.input_clk_src_freq_hz = 0;
  unit_config.output_clk_freq_hz = kSourceRateHz;
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

void run_case(size_t gate_words) {
  const size_t loop_words = gate_words * 2;
  const uint32_t tick_scale = kSampleRateHz / kRmtResolutionHz;
  const uint32_t expected_run = kSampleRateHz / kSourceRateHz;
  const size_t window_bytes = gate_words * expected_run;
  const uint32_t expected_duty_ppc =
      static_cast<uint32_t>(gate_words * 100 / loop_words);
  // window_bytes * (1 - spool / rate), with the rate in MB/s == MHz here.
  const size_t predicted_overload =
      window_bytes * (kSampleRateHz / 1000000 - kSpoolMBps) /
      (kSampleRateHz / 1000000);

  xQueueReset(capture_state.queue);
  capture_state.callback_count = 0;
  capture_state.callback_bytes = 0;
  capture_state.queue_overflow = 0;
  rmt_state.callback_count = 0;
  rmt_state.stored = 0;
  memset(rmt_state.durations, 0, sizeof(rmt_state.durations));
  memset(rmt_state.levels, 0, sizeof(rmt_state.levels));
  memset(ring_buffer, 0xA5, kRingSize);
  memset(destination, 0xA5, kDestinationSize);
  esp_cache_msync(ring_buffer, kRingSize, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  esp_err_t sync_result = esp_cache_msync(destination, kDestinationSize,
                                          ESP_CACHE_MSYNC_FLAG_DIR_C2M);

  parlio_rx_unit_handle_t rx_unit = nullptr;
  parlio_rx_delimiter_handle_t delimiter = nullptr;
  parlio_tx_unit_handle_t tx_unit = nullptr;
  rmt_channel_handle_t rmt_channel = nullptr;

  const esp_err_t config_result =
      create_receiver(kSampleRateHz, &rx_unit);

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

  esp_err_t rmt_result = ESP_FAIL;
  esp_err_t rmt_enable_result = ESP_FAIL;
  esp_err_t source_result = ESP_FAIL;
  esp_err_t source_start_result = ESP_FAIL;

  // E015 showed an input path can be added after an output path as long as
  // io_loop_back stays false. The variable here is whether RMT's input
  // survives PARLIO TX claiming the same pin as an output, or has to come
  // after it.
  // E045 showed the creation order does not matter, so fix RMT after the
  // source and vary nothing else.
  if (delimiter_result == ESP_OK) {
    build_pattern(gate_words, loop_words);
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
        tx_unit, source_pattern, loop_words * 8, &transmit_config);
  }

  if (source_start_result == ESP_OK) {
    rmt_result = create_rmt(&rmt_channel);
    if (rmt_result == ESP_OK) rmt_enable_result = rmt_enable(rmt_channel);
  }

  esp_err_t rmt_receive_result = ESP_FAIL;
  if (rmt_enable_result == ESP_OK && source_start_result == ESP_OK) {
    rmt_receive_config_t receive_config = {};
    receive_config.signal_range_min_ns = kRmtMinNs;
    receive_config.signal_range_max_ns = kRmtMaxNs;
    receive_config.flags.en_partial_rx = true;
    rmt_receive_result =
        rmt_receive(rmt_channel, rmt_buffer,
                    kRmtBufferSymbols * sizeof(rmt_symbol_word_t),
                    &receive_config);
  }

  esp_err_t enable_result = ESP_FAIL;
  if (rmt_receive_result == ESP_OK) {
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
  size_t consumed = 0;
  size_t dequeues = 0;
  size_t max_inflight = 0;
  int64_t harvest_us = 0;
  if (receive_result == ESP_OK) {
    const int64_t harvest_begin = esp_timer_get_time();
    while (esp_timer_get_time() - harvest_begin < kHarvestUs) {
      Chunk chunk = {};
      if (xQueueReceive(capture_state.queue, &chunk, pdMS_TO_TICKS(5)) !=
          pdTRUE) {
        continue;
      }
      const size_t copy_size = min(chunk.length, kDestinationSize - harvested);
      if (copy_size > 0) {
        memcpy(destination + harvested, chunk.data, copy_size);
        harvested += copy_size;
      }
      consumed += chunk.length;
      ++dequeues;
      // Bytes the DMA wrote into the ring that the task has not taken out.
      // Above kRingSize the ring has wrapped over unread data (E036).
      const size_t produced = capture_state.callback_bytes;
      max_inflight = max(max_inflight,
                         produced > consumed ? produced - consumed : 0);
    }
    harvest_us = esp_timer_get_time() - harvest_begin;
  }

  const esp_err_t disable_result =
      rx_unit != nullptr ? parlio_rx_unit_disable(rx_unit) : ESP_FAIL;
  const esp_err_t rmt_disable_result =
      rmt_channel != nullptr ? rmt_disable(rmt_channel) : ESP_FAIL;
  if (harvested > 0) {
    sync_result = esp_cache_msync(destination, kDestinationSize,
                                  ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (sync_result == ESP_OK) {
      sync_result = esp_cache_msync(destination, kDestinationSize,
                                    ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    }
  }

  size_t violations = 0;
  size_t shared_low = 0;
  size_t violation_offsets[kReportedViolations] = {};
  if (harvested > 1 && sync_result == ESP_OK) {
    // data_width 8 stores one sample per byte, so an offset is a sample index
    // and an RMT tick at 20 MHz is one byte of window.
    uint8_t previous = static_cast<uint8_t>(destination[0] & 0x7F);
    for (size_t sample = 0; sample < harvested; ++sample) {
      const uint8_t raw = destination[sample];
      if ((raw & (1u << kSharedBit)) == 0) ++shared_low;
      if (sample == 0) continue;
      const uint8_t value = static_cast<uint8_t>(raw & 0x7F);
      if (value == previous) continue;
      if (static_cast<uint8_t>((from_gray7(value) - from_gray7(previous)) &
                               0x7F) != 1) {
        if (violations < kReportedViolations) {
          violation_offsets[violations] = sample;
        }
        ++violations;
      }
      previous = value;
    }
  }

  size_t matched_steps = 0;
  size_t compared_steps = 0;
  const size_t reported =
      min(violations, static_cast<size_t>(kReportedViolations));
  for (size_t index = 1; index < reported; ++index) {
    if (violation_offsets[index] <= violation_offsets[index - 1]) continue;
    ++compared_steps;
    if (violation_offsets[index] - violation_offsets[index - 1] ==
        window_bytes) {
      ++matched_steps;
    }
  }

  uint32_t high_min = UINT32_MAX;
  uint32_t high_max = 0;
  uint32_t low_min = UINT32_MAX;
  uint32_t low_max = 0;
  size_t high_count = 0;
  size_t low_count = 0;
  for (size_t index = 0; index < rmt_state.stored; ++index) {
    const uint32_t duration = rmt_state.durations[index];
    if (duration == 0) continue;
    if (rmt_state.levels[index] != 0) {
      high_min = min(high_min, duration);
      high_max = max(high_max, duration);
      ++high_count;
    } else {
      low_min = min(low_min, duration);
      low_max = max(low_max, duration);
      ++low_count;
    }
  }
  if (high_min == UINT32_MAX) high_min = 0;
  if (low_min == UINT32_MAX) low_min = 0;

  const uint32_t harvest_kbps =
      harvest_us > 0
          ? static_cast<uint32_t>(harvested * 1000ULL / harvest_us)
          : 0;

  Serial.printf(
      "CASE gate_words=%lu loop_words=%lu window_bytes=%lu "
      "predicted_overload=%lu ring_mod_period=%lu tick_scale=%lu "
      "expected_run=%lu "
      "expected_duty_ppc=%lu config=%s delimiter=%s rmt=%s rmt_enable=%s "
      "source=%s source_start=%s rmt_receive=%s enable=%s receive=%s "
      "disable=%s rmt_disable=%s sync=%s rmt_callbacks=%lu rmt_stored=%lu "
      "high_count=%lu high_min=%lu high_max=%lu low_count=%lu low_min=%lu "
      "low_max=%lu parlio_callbacks=%lu dequeues=%lu harvested=%lu "
      "overflow=%lu violations=%lu shared_low=%lu inflight_max=%lu "
      "ring_bytes=%lu ring_overrun=%u matched_steps=%lu "
      "compared_steps=%lu harvest_us=%lld harvest_kbps=%lu\n",
      static_cast<unsigned long>(gate_words),
      static_cast<unsigned long>(loop_words),
      static_cast<unsigned long>(window_bytes),
      static_cast<unsigned long>(predicted_overload),
      static_cast<unsigned long>(kRingSize % 4096),
      static_cast<unsigned long>(tick_scale),
      static_cast<unsigned long>(expected_run),
      static_cast<unsigned long>(expected_duty_ppc),
      esp_err_to_name(config_result), esp_err_to_name(delimiter_result),
      esp_err_to_name(rmt_result), esp_err_to_name(rmt_enable_result),
      esp_err_to_name(source_result), esp_err_to_name(source_start_result),
      esp_err_to_name(rmt_receive_result), esp_err_to_name(enable_result),
      esp_err_to_name(receive_result), esp_err_to_name(disable_result),
      esp_err_to_name(rmt_disable_result), esp_err_to_name(sync_result),
      static_cast<unsigned long>(rmt_state.callback_count),
      static_cast<unsigned long>(rmt_state.stored),
      static_cast<unsigned long>(high_count),
      static_cast<unsigned long>(high_min),
      static_cast<unsigned long>(high_max),
      static_cast<unsigned long>(low_count),
      static_cast<unsigned long>(low_min),
      static_cast<unsigned long>(low_max),
      static_cast<unsigned long>(capture_state.callback_count),
      static_cast<unsigned long>(dequeues),
      static_cast<unsigned long>(harvested),
      static_cast<unsigned long>(capture_state.queue_overflow),
      static_cast<unsigned long>(violations),
      static_cast<unsigned long>(shared_low),
      static_cast<unsigned long>(max_inflight),
      static_cast<unsigned long>(kRingSize),
      static_cast<unsigned>(max_inflight > kRingSize ? 1 : 0),
      static_cast<unsigned long>(matched_steps),
      static_cast<unsigned long>(compared_steps), harvest_us,
      static_cast<unsigned long>(harvest_kbps));

  Serial.printf("SYMBOLS gate_words=%lu",
                static_cast<unsigned long>(gate_words));
  for (size_t index = 0; index < kReportedSymbols; ++index) {
    Serial.printf(" %u:%lu", static_cast<unsigned>(rmt_state.levels[index]),
                  static_cast<unsigned long>(rmt_state.durations[index]));
  }
  Serial.println();

  Serial.printf("VIOLS gate_words=%lu",
                static_cast<unsigned long>(gate_words));
  for (size_t index = 0; index < kReportedViolations; ++index) {
    Serial.printf(" %lu", static_cast<unsigned long>(violation_offsets[index]));
  }
  Serial.println();

  if (tx_unit != nullptr) {
    parlio_tx_unit_disable(tx_unit);
    parlio_del_tx_unit(tx_unit);
  }
  if (rmt_channel != nullptr) rmt_del_channel(rmt_channel);
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
      "ENV parlio_groups=%u rmt_rx_candidates=%u data_width=%u valid_pin=%d "
      "source_rate_hz=%lu source_words=%lu rmt_resolution_hz=%lu "
      "rmt_symbols=%u sample_rate_hz=%lu\n",
      static_cast<unsigned>(SOC_PARLIO_GROUPS),
      static_cast<unsigned>(SOC_RMT_RX_CANDIDATES_PER_GROUP),
      static_cast<unsigned>(kDataWidth), pins[kSharedBit],
      static_cast<unsigned long>(kSourceRateHz),
      static_cast<unsigned long>(kSourceWords),
      static_cast<unsigned long>(kRmtResolutionHz),
      static_cast<unsigned>(kReportedSymbols),
      static_cast<unsigned long>(kSampleRateHz));

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
  rmt_buffer = static_cast<rmt_symbol_word_t *>(heap_caps_aligned_alloc(
      internal_alignment, kRmtBufferSymbols * sizeof(rmt_symbol_word_t),
      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  capture_state.queue = xQueueCreate(kQueueDepth, sizeof(Chunk));
  Serial.printf(
      "BUFFERS ring_ok=%u source_ok=%u destination_ok=%u rmt_ok=%u queue_ok=%u\n",
      static_cast<unsigned>(ring_buffer != nullptr &&
                            esp_ptr_dma_capable(ring_buffer)),
      static_cast<unsigned>(source_pattern != nullptr &&
                            esp_ptr_dma_capable(source_pattern)),
      static_cast<unsigned>(destination != nullptr &&
                            esp_ptr_external_ram(destination)),
      static_cast<unsigned>(rmt_buffer != nullptr),
      static_cast<unsigned>(capture_state.queue != nullptr));
  if (ring_buffer == nullptr || source_pattern == nullptr ||
      destination == nullptr || rmt_buffer == nullptr ||
      capture_state.queue == nullptr) {
    Serial.println("DONE status=alloc-failed");
    return;
  }

  for (size_t gate_words : kGateWidths) run_case(gate_words);

  vQueueDelete(capture_state.queue);
  free(ring_buffer);
  free(source_pattern);
  free(destination);
  free(rmt_buffer);
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
