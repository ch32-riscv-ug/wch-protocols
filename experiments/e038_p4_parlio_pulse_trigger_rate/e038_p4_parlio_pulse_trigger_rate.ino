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
#include <soc/soc_caps.h>

#ifndef BANNER_GIT
#define BANNER_GIT "unknown"
#endif

#ifndef PARLIO_PINS
#define PARLIO_PINS "2,3,4,5,6,7,8,9"
#endif

#define EXPERIMENT_ID "E038"

namespace {

constexpr size_t kSourceLaneCount = 8;
// 8 declared pins and a valid line leave 4 as the largest driver-accepted
// data width for a hardware-triggered frame.
constexpr size_t kDataWidth = 4;
constexpr size_t kValidSourceBit = 4;
constexpr uint32_t kValidLineId = 4;
constexpr uint32_t kSourceDivider = 4;
constexpr size_t kSourceWords = 16384;
constexpr size_t kPulseIndex = 2048;
constexpr size_t kPulseWidth = 4;
constexpr size_t kFrameBytes = 16384;
constexpr size_t kFrameSamples = kFrameBytes * 8 / kDataWidth;
// The source loop is exactly twice the frame, so the gap between two frame
// completions is one loop period and gives the sample rate directly.
constexpr size_t kLoopSamples = kSourceWords * kSourceDivider;
constexpr size_t kFrameCount = 2;
constexpr int kWaitTimeoutMs = 1000;
constexpr uint32_t kRates[] = {
    20000000, 40000000, 80000000, 100000000, 120000000, 160000000,
};

struct EventState {
  volatile size_t receive_done;
  volatile size_t received_bytes[kFrameCount];
  volatile int64_t done_us[kFrameCount];
};

int pins[kSourceLaneCount] = {};
uint8_t *source_pattern = nullptr;
uint8_t *payloads[kFrameCount] = {};
EventState events = {};

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

bool IRAM_ATTR on_receive_done(parlio_rx_unit_handle_t,
                               const parlio_rx_event_data_t *event,
                               void *user_data) {
  auto *state = static_cast<EventState *>(user_data);
  const size_t slot = state->receive_done;
  if (slot < kFrameCount) {
    state->received_bytes[slot] = event->recv_bytes;
    state->done_us[slot] = esp_timer_get_time();
  }
  ++state->receive_done;
  return false;
}

void build_pattern() {
  for (size_t index = 0; index < kSourceWords; ++index) {
    uint8_t word = to_gray4(static_cast<uint8_t>(index & 0x0F));
    if (index >= kPulseIndex && index < kPulseIndex + kPulseWidth) {
      word |= static_cast<uint8_t>(1u << kValidSourceBit);
    }
    source_pattern[index] = word;
  }
  esp_cache_msync(source_pattern, kSourceWords, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

esp_err_t create_receiver(uint32_t sample_rate_hz,
                          parlio_rx_unit_handle_t *rx_unit) {
  parlio_rx_unit_config_t unit_config = {};
  unit_config.trans_queue_depth = kFrameCount;
  unit_config.max_recv_size = kFrameBytes;
  unit_config.data_width = kDataWidth;
  unit_config.clk_src = PARLIO_CLK_SRC_DEFAULT;
  unit_config.exp_clk_freq_hz = sample_rate_hz;
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
  callbacks.on_receive_done = on_receive_done;
  return parlio_rx_unit_register_event_callbacks(*rx_unit, &callbacks, &events);
}

esp_err_t create_source(uint32_t sample_rate_hz,
                        parlio_tx_unit_handle_t *tx_unit) {
  parlio_tx_unit_config_t unit_config = {};
  unit_config.clk_src = PARLIO_CLK_SRC_DEFAULT;
  unit_config.clk_in_gpio_num = GPIO_NUM_NC;
  unit_config.input_clk_src_freq_hz = 0;
  unit_config.output_clk_freq_hz = sample_rate_hz / kSourceDivider;
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

struct FrameStats {
  size_t runs;
  size_t violations;
  size_t run_min;
  size_t run_max;
  uint32_t head;
};

FrameStats verify_frame(const uint8_t *data) {
  FrameStats stats = {0, 0, 0, 0, 0};
  size_t smallest = SIZE_MAX;
  uint8_t previous = unpack_nibble(data, 0);
  size_t run = 1;
  for (size_t sample = 1; sample < kFrameSamples; ++sample) {
    const uint8_t value = unpack_nibble(data, sample);
    if (value == previous) {
      ++run;
      continue;
    }
    if (stats.runs != 0) {
      smallest = min(smallest, run);
      stats.run_max = max(stats.run_max, run);
    }
    if (static_cast<uint8_t>((from_gray4(value) - from_gray4(previous)) & 0x0F) !=
        1) {
      ++stats.violations;
    }
    ++stats.runs;
    run = 1;
    previous = value;
  }
  stats.run_min = smallest == SIZE_MAX ? 0 : smallest;
  for (size_t sample = 0; sample < 4; ++sample) {
    stats.head |= static_cast<uint32_t>(unpack_nibble(data, sample))
                  << (sample * 4);
  }
  return stats;
}

void run_case(uint32_t sample_rate_hz) {
  events.receive_done = 0;
  for (size_t frame = 0; frame < kFrameCount; ++frame) {
    events.received_bytes[frame] = 0;
    events.done_us[frame] = 0;
    memset(payloads[frame], 0xA5, kFrameBytes);
  }
  esp_err_t sync_result = ESP_OK;
  for (size_t frame = 0; frame < kFrameCount && sync_result == ESP_OK; ++frame) {
    sync_result = esp_cache_msync(payloads[frame], kFrameBytes,
                                  ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  }

  parlio_rx_unit_handle_t rx_unit = nullptr;
  parlio_rx_delimiter_handle_t delimiter = nullptr;
  parlio_tx_unit_handle_t tx_unit = nullptr;

  const esp_err_t config_result = create_receiver(sample_rate_hz, &rx_unit);

  esp_err_t delimiter_result = ESP_FAIL;
  if (config_result == ESP_OK) {
    parlio_rx_pulse_delimiter_config_t delimiter_config = {};
    delimiter_config.valid_sig_line_id = kValidLineId;
    delimiter_config.sample_edge = PARLIO_SAMPLE_EDGE_POS;
    delimiter_config.bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB;
    delimiter_config.eof_data_len = kFrameBytes;
    delimiter_config.timeout_ticks = 0;
    delimiter_config.flags.start_bit_included = false;
    delimiter_config.flags.end_bit_included = false;
    delimiter_config.flags.has_end_pulse = false;
    delimiter_config.flags.pulse_invert = false;
    delimiter_result =
        parlio_new_rx_pulse_delimiter(&delimiter_config, &delimiter);
  }

  esp_err_t source_result = ESP_FAIL;
  esp_err_t source_start_result = ESP_FAIL;
  if (delimiter_result == ESP_OK) {
    build_pattern();
    source_result = create_source(sample_rate_hz, &tx_unit);
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
  const int64_t begin = esp_timer_get_time();
  if (enable_result == ESP_OK && sync_result == ESP_OK) {
    parlio_receive_config_t receive_config = {};
    receive_config.delimiter = delimiter;
    receive_config.flags.partial_rx_en = false;
    receive_config.flags.indirect_mount = false;
    receive_result = ESP_OK;
    for (size_t frame = 0; frame < kFrameCount && receive_result == ESP_OK;
         ++frame) {
      receive_result = parlio_rx_unit_receive(rx_unit, payloads[frame],
                                             kFrameBytes, &receive_config);
    }
  }
  esp_err_t wait_result = ESP_FAIL;
  if (receive_result == ESP_OK) {
    wait_result = parlio_rx_unit_wait_all_done(rx_unit, kWaitTimeoutMs);
  }
  const int64_t elapsed_us = esp_timer_get_time() - begin;

  const esp_err_t disable_result =
      rx_unit != nullptr ? parlio_rx_unit_disable(rx_unit) : ESP_FAIL;
  for (size_t frame = 0; frame < kFrameCount && wait_result == ESP_OK; ++frame) {
    if (sync_result == ESP_OK) {
      sync_result = esp_cache_msync(payloads[frame], kFrameBytes,
                                    ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    }
  }

  FrameStats stats[kFrameCount] = {};
  const bool verified = wait_result == ESP_OK && sync_result == ESP_OK &&
                        events.receive_done >= kFrameCount;
  if (verified) {
    for (size_t frame = 0; frame < kFrameCount; ++frame) {
      stats[frame] = verify_frame(payloads[frame]);
    }
  }
  const int64_t gap_us =
      verified ? events.done_us[1] - events.done_us[0] : 0;
  const uint64_t measured_rate_hz =
      gap_us > 0 ? kLoopSamples * 1000000ULL / static_cast<uint64_t>(gap_us) : 0;
  const uint32_t ratio_ppk =
      measured_rate_hz > 0
          ? static_cast<uint32_t>(measured_rate_hz * 1000ULL / sample_rate_hz)
          : 0;

  Serial.printf(
      "CASE rx_rate_hz=%lu tx_rate_hz=%lu config=%s delimiter=%s source=%s "
      "source_start=%s enable=%s receive=%s wait=%s disable=%s sync=%s "
      "receive_done=%lu bytes0=%lu bytes1=%lu frame_bytes=%lu gap_us=%lld "
      "measured_rate_hz=%llu ratio_ppk=%lu runs0=%lu runs1=%lu run_min=%lu "
      "run_max=%lu violations=%lu head0=%08lx head1=%08lx elapsed_us=%lld\n",
      static_cast<unsigned long>(sample_rate_hz),
      static_cast<unsigned long>(sample_rate_hz / kSourceDivider),
      esp_err_to_name(config_result), esp_err_to_name(delimiter_result),
      esp_err_to_name(source_result), esp_err_to_name(source_start_result),
      esp_err_to_name(enable_result), esp_err_to_name(receive_result),
      esp_err_to_name(wait_result), esp_err_to_name(disable_result),
      esp_err_to_name(sync_result),
      static_cast<unsigned long>(events.receive_done),
      static_cast<unsigned long>(events.received_bytes[0]),
      static_cast<unsigned long>(events.received_bytes[1]),
      static_cast<unsigned long>(kFrameBytes), gap_us, measured_rate_hz,
      static_cast<unsigned long>(ratio_ppk),
      static_cast<unsigned long>(stats[0].runs),
      static_cast<unsigned long>(stats[1].runs),
      static_cast<unsigned long>(min(stats[0].run_min, stats[1].run_min)),
      static_cast<unsigned long>(max(stats[0].run_max, stats[1].run_max)),
      static_cast<unsigned long>(stats[0].violations + stats[1].violations),
      static_cast<unsigned long>(stats[0].head),
      static_cast<unsigned long>(stats[1].head), elapsed_us);

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
      "ENV parlio_groups=%u tx_units=%u rx_units=%u data_width=%u "
      "valid_pin=%d valid_line_id=%lu frame_samples=%lu loop_samples=%lu\n",
      static_cast<unsigned>(SOC_PARLIO_GROUPS),
      static_cast<unsigned>(SOC_PARLIO_TX_UNITS_PER_GROUP),
      static_cast<unsigned>(SOC_PARLIO_RX_UNITS_PER_GROUP),
      static_cast<unsigned>(kDataWidth), pins[kValidSourceBit],
      static_cast<unsigned long>(kValidLineId),
      static_cast<unsigned long>(kFrameSamples),
      static_cast<unsigned long>(kLoopSamples));

  size_t internal_alignment = 0;
  if (esp_cache_get_alignment(MALLOC_CAP_INTERNAL, &internal_alignment) !=
      ESP_OK) {
    Serial.println("DONE status=alignment-failed");
    return;
  }
  source_pattern = static_cast<uint8_t *>(heap_caps_aligned_alloc(
      internal_alignment, kSourceWords,
      MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  bool payloads_ok = true;
  for (size_t frame = 0; frame < kFrameCount; ++frame) {
    payloads[frame] = static_cast<uint8_t *>(heap_caps_aligned_alloc(
        internal_alignment, kFrameBytes,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
    payloads_ok = payloads_ok && payloads[frame] != nullptr &&
                  esp_ptr_dma_capable(payloads[frame]);
  }
  Serial.printf("BUFFERS source_ok=%u payload_ok=%u\n",
                static_cast<unsigned>(source_pattern != nullptr &&
                                      esp_ptr_dma_capable(source_pattern)),
                static_cast<unsigned>(payloads_ok));
  if (source_pattern == nullptr || !payloads_ok) {
    Serial.println("DONE status=alloc-failed");
    return;
  }

  for (uint32_t rate : kRates) run_case(rate);

  free(source_pattern);
  for (size_t frame = 0; frame < kFrameCount; ++frame) free(payloads[frame]);
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
