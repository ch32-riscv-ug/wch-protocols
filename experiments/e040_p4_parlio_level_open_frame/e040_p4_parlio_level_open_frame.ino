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

#define EXPERIMENT_ID "E040"

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
constexpr size_t kPayloadSize = 16 * 1024;
constexpr uint8_t kFill = 0xA5;
constexpr int kWaitTimeoutMs = 500;
constexpr int64_t kHarvestUs = 50000;
constexpr size_t kQueueDepth = 64;
// 20 MHz at data_width 4 packs two samples per byte.
constexpr uint32_t kRawByteRateKbps = kSampleRateHz / 1000 * kDataWidth / 8;

struct Case {
  bool partial;
  bool gate;
};

constexpr Case kCases[] = {
    {false, true},
    {true, true},
    {true, false},
};

struct Chunk {
  const uint8_t *data;
  size_t length;
};

struct EventState {
  QueueHandle_t queue;
  volatile size_t receive_done;
  volatile size_t partial_count;
  volatile size_t partial_bytes;
  volatile size_t queue_overflow;
};

int pins[kSourceLaneCount] = {};
uint8_t *source_pattern = nullptr;
uint8_t *payload = nullptr;
EventState events = {};

uint8_t to_gray4(uint8_t index) {
  return static_cast<uint8_t>((index ^ (index >> 1)) & 0x0F);
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
                               const parlio_rx_event_data_t *, void *user_data) {
  ++static_cast<EventState *>(user_data)->receive_done;
  return false;
}

bool IRAM_ATTR on_partial_receive(parlio_rx_unit_handle_t,
                                  const parlio_rx_event_data_t *event,
                                  void *user_data) {
  auto *state = static_cast<EventState *>(user_data);
  const Chunk chunk = {
      .data = static_cast<const uint8_t *>(event->data),
      .length = event->recv_bytes,
  };
  ++state->partial_count;
  state->partial_bytes += event->recv_bytes;
  BaseType_t high_task_woken = pdFALSE;
  if (xQueueSendFromISR(state->queue, &chunk, &high_task_woken) != pdTRUE) {
    ++state->queue_overflow;
  }
  return high_task_woken == pdTRUE;
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

esp_err_t create_receiver(parlio_rx_unit_handle_t *rx_unit, bool partial) {
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
  callbacks.on_partial_receive = partial ? on_partial_receive : nullptr;
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

void run_case(const Case &item) {
  xQueueReset(events.queue);
  events.receive_done = 0;
  events.partial_count = 0;
  events.partial_bytes = 0;
  events.queue_overflow = 0;
  memset(payload, kFill, kPayloadSize);
  esp_err_t sync_result =
      esp_cache_msync(payload, kPayloadSize, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

  parlio_rx_unit_handle_t rx_unit = nullptr;
  parlio_rx_delimiter_handle_t delimiter = nullptr;
  parlio_tx_unit_handle_t tx_unit = nullptr;

  const esp_err_t config_result = create_receiver(&rx_unit, item.partial);

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
    receive_config.flags.partial_rx_en = item.partial;
    receive_config.flags.indirect_mount = false;
    receive_result = parlio_rx_unit_receive(rx_unit, payload, kPayloadSize,
                                            &receive_config);
  }

  esp_err_t wait_result = ESP_FAIL;
  size_t harvested = 0;
  size_t dequeues = 0;
  int64_t harvest_us = 0;
  if (receive_result == ESP_OK && !item.partial) {
    wait_result = parlio_rx_unit_wait_all_done(rx_unit, kWaitTimeoutMs);
  } else if (receive_result == ESP_OK) {
    // Harvest for a fixed window instead of waiting for an EOF that the
    // open-ended delimiter may never produce.
    const int64_t harvest_begin = esp_timer_get_time();
    while (esp_timer_get_time() - harvest_begin < kHarvestUs) {
      Chunk chunk = {};
      if (xQueueReceive(events.queue, &chunk, pdMS_TO_TICKS(5)) != pdTRUE) {
        continue;
      }
      harvested += chunk.length;
      ++dequeues;
    }
    harvest_us = esp_timer_get_time() - harvest_begin;
    wait_result = ESP_ERR_NOT_FINISHED;
  }
  const int64_t elapsed_us = esp_timer_get_time() - begin;

  const esp_err_t disable_result =
      rx_unit != nullptr ? parlio_rx_unit_disable(rx_unit) : ESP_FAIL;
  // Look at the payload whatever happened: that is the whole point here.
  const esp_err_t inspect_sync =
      esp_cache_msync(payload, kPayloadSize, ESP_CACHE_MSYNC_FLAG_DIR_M2C);

  size_t written = 0;
  size_t first_written = kPayloadSize;
  size_t last_written = 0;
  if (inspect_sync == ESP_OK) {
    for (size_t offset = 0; offset < kPayloadSize; ++offset) {
      if (payload[offset] == kFill) continue;
      ++written;
      if (first_written == kPayloadSize) first_written = offset;
      last_written = offset;
    }
  }
  uint64_t head = 0;
  for (size_t offset = 0; offset < 8; ++offset) {
    head |= static_cast<uint64_t>(payload[offset]) << (offset * 8);
  }

  const uint32_t harvest_kbps =
      harvest_us > 0
          ? static_cast<uint32_t>(harvested * 1000ULL / harvest_us)
          : 0;
  const uint32_t raw_ratio_ppc =
      harvest_kbps > 0 ? harvest_kbps * 100 / kRawByteRateKbps : 0;

  Serial.printf(
      "CASE partial=%u gate=%u config=%s delimiter=%s source=%s "
      "source_start=%s enable=%s receive=%s wait=%s disable=%s sync=%s "
      "inspect_sync=%s receive_done=%lu partial_count=%lu partial_bytes=%lu "
      "dequeues=%lu harvested=%lu overflow=%lu written_bytes=%lu "
      "first_written=%lu last_written=%lu payload_size=%lu head=%016llx "
      "harvest_us=%lld harvest_kbps=%lu raw_kbps=%lu raw_ratio_ppc=%lu "
      "elapsed_us=%lld\n",
      static_cast<unsigned>(item.partial ? 1 : 0),
      static_cast<unsigned>(item.gate ? 1 : 0), esp_err_to_name(config_result),
      esp_err_to_name(delimiter_result), esp_err_to_name(source_result),
      esp_err_to_name(source_start_result), esp_err_to_name(enable_result),
      esp_err_to_name(receive_result), esp_err_to_name(wait_result),
      esp_err_to_name(disable_result), esp_err_to_name(sync_result),
      esp_err_to_name(inspect_sync),
      static_cast<unsigned long>(events.receive_done),
      static_cast<unsigned long>(events.partial_count),
      static_cast<unsigned long>(events.partial_bytes),
      static_cast<unsigned long>(dequeues),
      static_cast<unsigned long>(harvested),
      static_cast<unsigned long>(events.queue_overflow),
      static_cast<unsigned long>(written),
      static_cast<unsigned long>(first_written == kPayloadSize ? 0
                                                               : first_written),
      static_cast<unsigned long>(last_written),
      static_cast<unsigned long>(kPayloadSize),
      static_cast<unsigned long long>(head), harvest_us,
      static_cast<unsigned long>(harvest_kbps),
      static_cast<unsigned long>(kRawByteRateKbps),
      static_cast<unsigned long>(raw_ratio_ppc), elapsed_us);

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
      "sample_rate_hz=%lu gate_words=%lu source_words=%lu fill=%02x\n",
      static_cast<unsigned>(SOC_PARLIO_GROUPS),
      static_cast<unsigned>(SOC_PARLIO_RX_UNITS_PER_GROUP),
      static_cast<unsigned>(kDataWidth), pins[kValidSourceBit],
      static_cast<unsigned long>(kSampleRateHz),
      static_cast<unsigned long>(kGateWords),
      static_cast<unsigned long>(kSourceWords), static_cast<unsigned>(kFill));

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
  events.queue = xQueueCreate(kQueueDepth, sizeof(Chunk));
  Serial.printf("BUFFERS source_ok=%u payload_ok=%u queue_ok=%u\n",
                static_cast<unsigned>(source_pattern != nullptr &&
                                      esp_ptr_dma_capable(source_pattern)),
                static_cast<unsigned>(payload != nullptr &&
                                      esp_ptr_dma_capable(payload)),
                static_cast<unsigned>(events.queue != nullptr));
  if (source_pattern == nullptr || payload == nullptr ||
      events.queue == nullptr) {
    Serial.println("DONE status=alloc-failed");
    return;
  }

  for (const Case &item : kCases) run_case(item);

  vQueueDelete(events.queue);
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
