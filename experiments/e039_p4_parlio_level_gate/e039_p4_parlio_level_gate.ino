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

#define EXPERIMENT_ID "E039"

namespace {

constexpr size_t kSourceLaneCount = 8;
constexpr size_t kDataWidth = 4;
constexpr size_t kValidSourceBit = 4;
constexpr uint32_t kValidLineId = 4;
constexpr uint32_t kSampleRateHz = 20000000;
constexpr uint32_t kSourceDivider = 4;
constexpr size_t kSourceWords = 16384;
constexpr size_t kGateIndex = 2048;
constexpr size_t kGateWords = 2048;
constexpr size_t kFixedFrameBytes = 2048;
constexpr size_t kPayloadSize = 16 * 1024;
// A gate of kGateWords source words spans kGateWords * divider samples, and
// data_width 4 packs two samples per byte.
constexpr size_t kGateBytes = kGateWords * kSourceDivider * kDataWidth / 8;
constexpr int kWaitTimeoutMs = 500;

struct Case {
  bool active_low;
  uint32_t eof_data_len;
  bool gate;
  size_t expected_bytes;
};

constexpr Case kCases[] = {
    {false, kFixedFrameBytes, true, kFixedFrameBytes},
    {false, 0, true, kGateBytes},
    {true, kFixedFrameBytes, true, kFixedFrameBytes},
    {false, kFixedFrameBytes, false, 0},
};

struct EventState {
  volatile size_t receive_done;
  volatile size_t received_bytes;
};

int pins[kSourceLaneCount] = {};
uint8_t *source_pattern = nullptr;
uint8_t *payload = nullptr;
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
  ++state->receive_done;
  state->received_bytes += event->recv_bytes;
  return false;
}

void build_pattern(bool gate) {
  for (size_t index = 0; index < kSourceWords; ++index) {
    uint8_t word = to_gray4(static_cast<uint8_t>(index & 0x0F));
    if (gate && index >= kGateIndex && index < kGateIndex + kGateWords) {
      word |= static_cast<uint8_t>(1u << kValidSourceBit);
    }
    source_pattern[index] = word;
  }
  esp_cache_msync(source_pattern, kSourceWords, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

esp_err_t create_receiver(parlio_rx_unit_handle_t *rx_unit) {
  parlio_rx_unit_config_t unit_config = {};
  unit_config.trans_queue_depth = 1;
  unit_config.max_recv_size = kPayloadSize;
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
  callbacks.on_receive_done = on_receive_done;
  return parlio_rx_unit_register_event_callbacks(*rx_unit, &callbacks, &events);
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

void run_case(const Case &item) {
  events.receive_done = 0;
  events.received_bytes = 0;
  memset(payload, 0xA5, kPayloadSize);
  esp_err_t sync_result =
      esp_cache_msync(payload, kPayloadSize, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

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
    delimiter_config.eof_data_len = item.eof_data_len;
    delimiter_config.timeout_ticks = 0;
    delimiter_config.flags.active_low_en = item.active_low;
    delimiter_result =
        parlio_new_rx_level_delimiter(&delimiter_config, &delimiter);
  }

  esp_err_t source_result = ESP_FAIL;
  esp_err_t source_start_result = ESP_FAIL;
  if (delimiter_result == ESP_OK) {
    build_pattern(item.gate);
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
  const int64_t begin = esp_timer_get_time();
  if (enable_result == ESP_OK && sync_result == ESP_OK) {
    parlio_receive_config_t receive_config = {};
    receive_config.delimiter = delimiter;
    receive_config.flags.partial_rx_en = false;
    receive_config.flags.indirect_mount = false;
    receive_result = parlio_rx_unit_receive(rx_unit, payload, kPayloadSize,
                                            &receive_config);
  }
  esp_err_t wait_result = ESP_FAIL;
  if (receive_result == ESP_OK) {
    wait_result = parlio_rx_unit_wait_all_done(rx_unit, kWaitTimeoutMs);
  }
  const int64_t elapsed_us = esp_timer_get_time() - begin;

  const esp_err_t disable_result =
      rx_unit != nullptr ? parlio_rx_unit_disable(rx_unit) : ESP_FAIL;
  if (wait_result == ESP_OK) {
    sync_result = esp_cache_msync(payload, kPayloadSize,
                                  ESP_CACHE_MSYNC_FLAG_DIR_M2C);
  }

  size_t runs = 0;
  size_t violations = 0;
  size_t run_min = 0;
  size_t run_max = 0;
  uint32_t head = 0;
  const size_t received = events.received_bytes;
  if (wait_result == ESP_OK && sync_result == ESP_OK && received > 0 &&
      received <= kPayloadSize) {
    const size_t samples = received * 8 / kDataWidth;
    size_t smallest = SIZE_MAX;
    uint8_t previous = unpack_nibble(payload, 0);
    size_t run = 1;
    for (size_t sample = 1; sample < samples; ++sample) {
      const uint8_t value = unpack_nibble(payload, sample);
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
        ++violations;
      }
      ++runs;
      run = 1;
      previous = value;
    }
    run_min = smallest == SIZE_MAX ? 0 : smallest;
    for (size_t sample = 0; sample < 4; ++sample) {
      head |= static_cast<uint32_t>(unpack_nibble(payload, sample))
              << (sample * 4);
    }
  }

  const long delta_bytes = static_cast<long>(received) -
                           static_cast<long>(item.expected_bytes);
  Serial.printf(
      "CASE active_low=%u eof_data_len=%lu gate=%u config=%s delimiter=%s "
      "source=%s source_start=%s enable=%s receive=%s wait=%s disable=%s "
      "sync=%s receive_done=%lu received_bytes=%lu expected_bytes=%lu "
      "delta_bytes=%ld runs=%lu run_min=%lu run_max=%lu violations=%lu "
      "head=%08lx elapsed_us=%lld\n",
      static_cast<unsigned>(item.active_low ? 1 : 0),
      static_cast<unsigned long>(item.eof_data_len),
      static_cast<unsigned>(item.gate ? 1 : 0), esp_err_to_name(config_result),
      esp_err_to_name(delimiter_result), esp_err_to_name(source_result),
      esp_err_to_name(source_start_result), esp_err_to_name(enable_result),
      esp_err_to_name(receive_result), esp_err_to_name(wait_result),
      esp_err_to_name(disable_result), esp_err_to_name(sync_result),
      static_cast<unsigned long>(events.receive_done),
      static_cast<unsigned long>(received),
      static_cast<unsigned long>(item.expected_bytes), delta_bytes,
      static_cast<unsigned long>(runs), static_cast<unsigned long>(run_min),
      static_cast<unsigned long>(run_max),
      static_cast<unsigned long>(violations),
      static_cast<unsigned long>(head), elapsed_us);

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
      "valid_line_id=%lu sample_rate_hz=%lu gate_words=%lu gate_bytes=%lu\n",
      static_cast<unsigned>(SOC_PARLIO_GROUPS),
      static_cast<unsigned>(SOC_PARLIO_RX_UNITS_PER_GROUP),
      static_cast<unsigned>(kDataWidth), pins[kValidSourceBit],
      static_cast<unsigned long>(kValidLineId),
      static_cast<unsigned long>(kSampleRateHz),
      static_cast<unsigned long>(kGateWords),
      static_cast<unsigned long>(kGateBytes));

  size_t internal_alignment = 0;
  if (esp_cache_get_alignment(MALLOC_CAP_INTERNAL, &internal_alignment) !=
      ESP_OK) {
    Serial.println("DONE status=alignment-failed");
    return;
  }
  source_pattern = static_cast<uint8_t *>(heap_caps_aligned_alloc(
      internal_alignment, kSourceWords,
      MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  payload = static_cast<uint8_t *>(heap_caps_aligned_alloc(
      internal_alignment, kPayloadSize,
      MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  Serial.printf("BUFFERS source_ok=%u payload_ok=%u\n",
                static_cast<unsigned>(source_pattern != nullptr &&
                                      esp_ptr_dma_capable(source_pattern)),
                static_cast<unsigned>(payload != nullptr &&
                                      esp_ptr_dma_capable(payload)));
  if (source_pattern == nullptr || payload == nullptr) {
    Serial.println("DONE status=alloc-failed");
    return;
  }

  for (const Case &item : kCases) run_case(item);

  free(source_pattern);
  free(payload);
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
