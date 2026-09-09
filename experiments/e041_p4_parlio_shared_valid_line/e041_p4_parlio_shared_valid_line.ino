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

#define EXPERIMENT_ID "E041"

namespace {

constexpr size_t kDataWidth = 8;
// The valid signal shares the last data pin: the GPIO matrix can fan one
// input GPIO out to several peripheral input signals.
constexpr size_t kSharedBit = 7;
constexpr uint32_t kValidLineId = 8;
constexpr uint32_t kSourceDivider = 4;
constexpr size_t kSourceWords = 16384;
constexpr size_t kGateIndex = 2048;
constexpr size_t kGateWords = 2048;
constexpr size_t kFrameBytes = 4096;
constexpr size_t kPayloadSize = 8 * 1024;
// data_width 8 stores one sample per byte.
constexpr size_t kFrameSamples = kFrameBytes;
constexpr int kWaitTimeoutMs = 500;
constexpr uint32_t kRates[] = {20000000, 80000000, 160000000};

struct EventState {
  volatile size_t receive_done;
  volatile size_t received_bytes;
};

int pins[kDataWidth] = {};
uint8_t *source_pattern = nullptr;
uint8_t *payload = nullptr;
EventState events = {};

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
  for (size_t lane = 0; lane < kDataWidth; ++lane) {
    char *end = nullptr;
    const long value = strtol(cursor, &end, 10);
    if (end == cursor || value < 0 || value >= GPIO_NUM_MAX) return false;
    pins[lane] = static_cast<int>(value);
    if (lane + 1 < kDataWidth) {
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

void build_pattern() {
  for (size_t index = 0; index < kSourceWords; ++index) {
    uint8_t word = to_gray7(static_cast<uint8_t>(index & 0x7F));
    if (index >= kGateIndex && index < kGateIndex + kGateWords) {
      word |= static_cast<uint8_t>(1u << kSharedBit);
    }
    source_pattern[index] = word;
  }
  esp_cache_msync(source_pattern, kSourceWords, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

esp_err_t create_receiver(uint32_t sample_rate_hz,
                          parlio_rx_unit_handle_t *rx_unit) {
  parlio_rx_unit_config_t unit_config = {};
  unit_config.trans_queue_depth = 1;
  unit_config.max_recv_size = kFrameBytes;
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
  unit_config.data_width = kDataWidth;
  for (size_t lane = 0; lane < PARLIO_TX_UNIT_MAX_DATA_WIDTH; ++lane) {
    unit_config.data_gpio_nums[lane] =
        lane < kDataWidth ? static_cast<gpio_num_t>(pins[lane]) : GPIO_NUM_NC;
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

void run_case(uint32_t sample_rate_hz) {
  events.receive_done = 0;
  events.received_bytes = 0;
  memset(payload, 0xA5, kPayloadSize);
  esp_err_t sync_result =
      esp_cache_msync(payload, kPayloadSize, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

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
    receive_result = parlio_rx_unit_receive(rx_unit, payload, kFrameBytes,
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
  size_t shared_low = 0;
  uint32_t head = 0;
  if (wait_result == ESP_OK && sync_result == ESP_OK &&
      events.received_bytes >= kFrameBytes) {
    size_t smallest = SIZE_MAX;
    uint8_t previous = static_cast<uint8_t>(payload[0] & 0x7F);
    size_t run = 1;
    for (size_t sample = 0; sample < kFrameSamples; ++sample) {
      const uint8_t raw = payload[sample];
      if ((raw & (1u << kSharedBit)) == 0) ++shared_low;
      if (sample == 0) continue;
      const uint8_t value = static_cast<uint8_t>(raw & 0x7F);
      if (value == previous) {
        ++run;
        continue;
      }
      if (runs != 0) {
        smallest = min(smallest, run);
        run_max = max(run_max, run);
      }
      if (static_cast<uint8_t>((from_gray7(value) - from_gray7(previous)) &
                               0x7F) != 1) {
        ++violations;
      }
      ++runs;
      run = 1;
      previous = value;
    }
    run_min = smallest == SIZE_MAX ? 0 : smallest;
    for (size_t sample = 0; sample < 4; ++sample) {
      head |= static_cast<uint32_t>(payload[sample]) << (sample * 8);
    }
  }

  Serial.printf(
      "CASE rx_rate_hz=%lu tx_rate_hz=%lu shared_pin=%d valid_line_id=%lu "
      "config=%s delimiter=%s source=%s source_start=%s enable=%s receive=%s "
      "wait=%s disable=%s sync=%s receive_done=%lu received_bytes=%lu "
      "frame_bytes=%lu runs=%lu run_min=%lu run_max=%lu violations=%lu "
      "shared_low=%lu head=%08lx elapsed_us=%lld\n",
      static_cast<unsigned long>(sample_rate_hz),
      static_cast<unsigned long>(sample_rate_hz / kSourceDivider),
      pins[kSharedBit], static_cast<unsigned long>(kValidLineId),
      esp_err_to_name(config_result), esp_err_to_name(delimiter_result),
      esp_err_to_name(source_result), esp_err_to_name(source_start_result),
      esp_err_to_name(enable_result), esp_err_to_name(receive_result),
      esp_err_to_name(wait_result), esp_err_to_name(disable_result),
      esp_err_to_name(sync_result),
      static_cast<unsigned long>(events.receive_done),
      static_cast<unsigned long>(events.received_bytes),
      static_cast<unsigned long>(kFrameBytes),
      static_cast<unsigned long>(runs), static_cast<unsigned long>(run_min),
      static_cast<unsigned long>(run_max),
      static_cast<unsigned long>(violations),
      static_cast<unsigned long>(shared_low),
      static_cast<unsigned long>(head), elapsed_us);

  if (tx_unit != nullptr) {
    parlio_tx_unit_disable(tx_unit);
    parlio_del_tx_unit(tx_unit);
  }
  if (delimiter != nullptr) parlio_del_rx_delimiter(delimiter);
  if (rx_unit != nullptr) parlio_del_rx_unit(rx_unit);
  for (size_t lane = 0; lane < kDataWidth; ++lane) {
    gpio_reset_pin(static_cast<gpio_num_t>(pins[lane]));
  }
}

void run_experiment() {
  Serial.print("# EXP " EXPERIMENT_ID " v1 git=");
  Serial.print(BANNER_GIT);
  Serial.print(" probe=esp32p4_parlio target=internal build=");
  Serial.println(__DATE__ " " __TIME__);
  Serial.printf(
      "ENV parlio_groups=%u rx_units=%u data_width=%u shared_bit=%u "
      "valid_line_id=%lu frame_samples=%lu gate_words=%lu\n",
      static_cast<unsigned>(SOC_PARLIO_GROUPS),
      static_cast<unsigned>(SOC_PARLIO_RX_UNITS_PER_GROUP),
      static_cast<unsigned>(kDataWidth), static_cast<unsigned>(kSharedBit),
      static_cast<unsigned long>(kValidLineId),
      static_cast<unsigned long>(kFrameSamples),
      static_cast<unsigned long>(kGateWords));

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

  for (uint32_t rate : kRates) run_case(rate);

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
