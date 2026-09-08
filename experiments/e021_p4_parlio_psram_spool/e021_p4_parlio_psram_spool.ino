// E021: spool a continuous internal PARLIO DMA ring into a 1 MiB PSRAM buffer.
// Plan and report: README.ja.md

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/parlio_rx.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <esp_private/esp_cache_private.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#ifndef BANNER_GIT
#define BANNER_GIT "unknown"
#endif

#ifndef PARLIO_PINS
#define PARLIO_PINS "2,3,4,5,6,7,8,9"
#endif

#ifndef EXPERIMENT_ID
#define EXPERIMENT_ID "E021"
#endif

#ifndef SAMPLE_RATE_HZ
#define SAMPLE_RATE_HZ 8000000
#endif

namespace {

constexpr size_t kLaneCount = 8;
constexpr uint32_t kPwmFrequencyHz = 100000;
constexpr uint8_t kPwmResolutionBits = 8;
constexpr uint32_t kSampleRateHz = SAMPLE_RATE_HZ;
constexpr size_t kRingSize = 64 * 1024;
constexpr size_t kDestinationSize = 1024 * 1024;
constexpr size_t kDelimiterSize = 65408;
constexpr size_t kQueueDepth = 64;
constexpr size_t kRuns = 3;
constexpr uint32_t kDuties[kLaneCount] = {
    16, 48, 80, 112, 144, 176, 208, 240,
};

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

int pins[kLaneCount] = {};
bool ledc_attached[kLaneCount] = {};
uint8_t *ring_buffer = nullptr;
uint8_t *destination = nullptr;
CaptureState capture_state = {};

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

esp_err_t configure_pwm() {
  for (size_t lane = 0; lane < kLaneCount; ++lane) {
    if (!ledcAttachChannel(pins[lane], kPwmFrequencyHz, kPwmResolutionBits,
                           lane)) return ESP_FAIL;
    ledc_attached[lane] = true;
    if (!ledcWrite(pins[lane], kDuties[lane])) return ESP_FAIL;
  }
  return ESP_OK;
}

void cleanup_pwm() {
  for (size_t lane = 0; lane < kLaneCount; ++lane) {
    if (ledc_attached[lane]) {
      ledcDetach(pins[lane]);
      ledc_attached[lane] = false;
    }
    gpio_reset_pin(static_cast<gpio_num_t>(pins[lane]));
  }
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

esp_err_t create_receiver(parlio_rx_unit_handle_t *rx_unit,
                          parlio_rx_delimiter_handle_t *delimiter) {
  parlio_rx_unit_config_t unit_config = {};
  unit_config.trans_queue_depth = 1;
  unit_config.max_recv_size = kRingSize;
  unit_config.dma_burst_size = 0;
  unit_config.data_width = kLaneCount;
  unit_config.clk_src = PARLIO_CLK_SRC_DEFAULT;
  unit_config.exp_clk_freq_hz = kSampleRateHz;
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
  result = parlio_rx_unit_register_event_callbacks(
      *rx_unit, &callbacks, &capture_state);
  if (result != ESP_OK) return result;

  parlio_rx_soft_delimiter_config_t delimiter_config = {};
  delimiter_config.sample_edge = PARLIO_SAMPLE_EDGE_POS;
  delimiter_config.bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB;
  delimiter_config.eof_data_len = kDelimiterSize;
  return parlio_new_rx_soft_delimiter(&delimiter_config, delimiter);
}

void run_case(size_t run) {
  xQueueReset(capture_state.queue);
  capture_state.callback_count = 0;
  capture_state.callback_bytes = 0;
  capture_state.queue_overflow = 0;
  memset(ring_buffer, 0xA5, kRingSize);
  memset(destination, 0xA5, kDestinationSize);
  esp_cache_msync(ring_buffer, kRingSize, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  esp_err_t sync_result = esp_cache_msync(
      destination, kDestinationSize, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

  parlio_rx_unit_handle_t rx_unit = nullptr;
  parlio_rx_delimiter_handle_t delimiter = nullptr;
  const esp_err_t config_result = create_receiver(&rx_unit, &delimiter);
  esp_err_t enable_result = ESP_FAIL;
  if (config_result == ESP_OK) {
    enable_result = parlio_rx_unit_enable(rx_unit, true);
  }
  parlio_receive_config_t receive_config = {};
  receive_config.delimiter = delimiter;
  receive_config.flags.partial_rx_en = true;
  receive_config.flags.indirect_mount = false;
  esp_err_t receive_result = ESP_FAIL;
  if (enable_result == ESP_OK && sync_result == ESP_OK) {
    receive_result = parlio_rx_unit_receive(
        rx_unit, ring_buffer, kRingSize, &receive_config);
  }
  esp_err_t start_result = ESP_FAIL;
  size_t copied = 0;
  size_t dequeue_count = 0;
  size_t min_chunk = SIZE_MAX;
  size_t max_chunk = 0;
  size_t max_queue_depth = 0;
  bool timed_out = false;
  const int64_t capture_begin = esp_timer_get_time();
  if (receive_result == ESP_OK) {
    start_result =
        parlio_rx_soft_delimiter_start_stop(rx_unit, delimiter, true);
  }
  while (start_result == ESP_OK && copied < kDestinationSize) {
    Chunk chunk = {};
    if (xQueueReceive(capture_state.queue, &chunk,
                      pdMS_TO_TICKS(500)) != pdTRUE) {
      timed_out = true;
      break;
    }
    const size_t remaining = kDestinationSize - copied;
    const size_t copy_size = min(chunk.length, remaining);
    memcpy(destination + copied, chunk.data, copy_size);
    copied += copy_size;
    ++dequeue_count;
    min_chunk = min(min_chunk, chunk.length);
    max_chunk = max(max_chunk, chunk.length);
    max_queue_depth = max(max_queue_depth,
                          static_cast<size_t>(uxQueueMessagesWaiting(
                              capture_state.queue)));
  }
  const int64_t capture_us = esp_timer_get_time() - capture_begin;
  esp_err_t stop_result = ESP_FAIL;
  if (start_result == ESP_OK) {
    stop_result =
        parlio_rx_soft_delimiter_start_stop(rx_unit, delimiter, false);
  }
  const esp_err_t disable_result = parlio_rx_unit_disable(rx_unit);

  int64_t sync_us = 0;
  if (!timed_out && copied == kDestinationSize && disable_result == ESP_OK) {
    const int64_t sync_begin = esp_timer_get_time();
    sync_result = esp_cache_msync(
        destination, kDestinationSize, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (sync_result == ESP_OK) {
      sync_result = esp_cache_msync(
          destination, kDestinationSize, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    }
    sync_us = esp_timer_get_time() - sync_begin;
  } else if (timed_out) {
    sync_result = ESP_ERR_TIMEOUT;
  }

  esp_err_t result = enable_result;
  if (result == ESP_OK) result = receive_result;
  if (result == ESP_OK) result = start_result;
  if (result == ESP_OK && timed_out) result = ESP_ERR_TIMEOUT;
  if (result == ESP_OK) result = stop_result;
  if (result == ESP_OK) result = disable_result;
  if (result == ESP_OK) result = sync_result;
  if (result == ESP_OK && capture_state.queue_overflow != 0) result = ESP_FAIL;

  uint32_t max_error_ppm = 0;
  size_t min_edges = SIZE_MAX;
  size_t max_edges = 0;
  if (result == ESP_OK) {
    for (size_t lane = 0; lane < kLaneCount; ++lane) {
      const uint8_t mask = 1U << lane;
      size_t high = 0;
      size_t edges = 0;
      for (size_t sample = 0; sample < kDestinationSize; ++sample) {
        high += (destination[sample] & mask) != 0;
        if (sample != 0 &&
            ((destination[sample - 1] ^ destination[sample]) & mask) != 0) {
          ++edges;
        }
      }
      const uint32_t ratio_ppm = high * 1000000ULL / kDestinationSize;
      const uint32_t expected_ppm = kDuties[lane] * 1000000ULL / 256;
      const uint32_t error_ppm = ratio_ppm > expected_ppm
                                     ? ratio_ppm - expected_ppm
                                     : expected_ppm - ratio_ppm;
      max_error_ppm = max(max_error_ppm, error_ppm);
      min_edges = min(min_edges, edges);
      max_edges = max(max_edges, edges);
    }
  } else {
    min_edges = 0;
  }

  const size_t callback_count = capture_state.callback_count;
  const size_t callback_bytes = capture_state.callback_bytes;
  const size_t overflows = capture_state.queue_overflow;
  const uint64_t rate_mbps_milli =
      capture_us > 0 ? copied * 1000ULL / capture_us : 0;
  Serial.print("CASE run=");
  Serial.print(run);
  Serial.print(" result=");
  Serial.print(esp_err_to_name(result));
  Serial.print(" config=");
  Serial.print(esp_err_to_name(config_result));
  Serial.print(" enable=");
  Serial.print(esp_err_to_name(enable_result));
  Serial.print(" receive=");
  Serial.print(esp_err_to_name(receive_result));
  Serial.print(" start=");
  Serial.print(esp_err_to_name(start_result));
  Serial.print(" stop=");
  Serial.print(esp_err_to_name(stop_result));
  Serial.print(" disable=");
  Serial.print(esp_err_to_name(disable_result));
  Serial.print(" sync=");
  Serial.print(esp_err_to_name(sync_result));
  Serial.print(" callbacks=");
  Serial.print(callback_count);
  Serial.print(" dequeues=");
  Serial.print(dequeue_count);
  Serial.print(" callback_bytes=");
  Serial.print(callback_bytes);
  Serial.print(" copied=");
  Serial.print(copied);
  Serial.print(" extra_bytes=");
  Serial.print(callback_bytes > copied ? callback_bytes - copied : 0);
  Serial.print(" overflows=");
  Serial.print(overflows);
  Serial.print(" min_chunk=");
  Serial.print(min_chunk == SIZE_MAX ? 0 : min_chunk);
  Serial.print(" max_chunk=");
  Serial.print(max_chunk);
  Serial.print(" max_queue=");
  Serial.print(max_queue_depth);
  Serial.print(" capture_us=");
  Serial.print(capture_us);
  Serial.print(" rate_mbps_milli=");
  Serial.print(rate_mbps_milli);
  Serial.print(" sync_us=");
  Serial.print(sync_us);
  Serial.print(" max_error_ppm=");
  Serial.print(max_error_ppm);
  Serial.print(" min_edges=");
  Serial.print(min_edges);
  Serial.print(" max_edges=");
  Serial.println(max_edges);

  if (delimiter != nullptr) parlio_del_rx_delimiter(delimiter);
  if (rx_unit != nullptr) parlio_del_rx_unit(rx_unit);
}

void run_experiment() {
  Serial.print("# EXP " EXPERIMENT_ID " v1 git=");
  Serial.print(BANNER_GIT);
  Serial.print(" probe=esp32p4_parlio target=internal build=");
  Serial.println(__DATE__ " " __TIME__);

  size_t internal_alignment = 0;
  size_t external_alignment = 0;
  const esp_err_t internal_align_result =
      esp_cache_get_alignment(MALLOC_CAP_INTERNAL, &internal_alignment);
  const esp_err_t external_align_result =
      esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &external_alignment);
  Serial.print("ENV psram_found=");
  Serial.print(psramFound());
  Serial.print(" psram_size=");
  Serial.print(ESP.getPsramSize());
  Serial.print(" int_align_result=");
  Serial.print(esp_err_to_name(internal_align_result));
  Serial.print(" int_align=");
  Serial.print(internal_alignment);
  Serial.print(" ext_align_result=");
  Serial.print(esp_err_to_name(external_align_result));
  Serial.print(" ext_align=");
  Serial.println(external_alignment);
  if (!psramFound() || internal_align_result != ESP_OK ||
      external_align_result != ESP_OK) {
    Serial.println("DONE status=environment-failed");
    return;
  }

  ring_buffer = static_cast<uint8_t *>(heap_caps_aligned_alloc(
      internal_alignment, kRingSize,
      MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  destination = static_cast<uint8_t *>(heap_caps_aligned_alloc(
      external_alignment, kDestinationSize,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  capture_state.queue = xQueueCreate(kQueueDepth, sizeof(Chunk));
  Serial.print("BUFFERS ring=");
  Serial.print(reinterpret_cast<uintptr_t>(ring_buffer), HEX);
  Serial.print(" ring_internal=");
  Serial.print(ring_buffer != nullptr && esp_ptr_internal(ring_buffer));
  Serial.print(" ring_dma=");
  Serial.print(ring_buffer != nullptr && esp_ptr_dma_capable(ring_buffer));
  Serial.print(" destination=");
  Serial.print(reinterpret_cast<uintptr_t>(destination), HEX);
  Serial.print(" destination_external=");
  Serial.print(destination != nullptr && esp_ptr_external_ram(destination));
  Serial.print(" queue=");
  Serial.println(capture_state.queue != nullptr);
  if (ring_buffer == nullptr || destination == nullptr ||
      capture_state.queue == nullptr) {
    Serial.println("DONE status=alloc-failed");
    return;
  }

  const esp_err_t pwm_result = configure_pwm();
  Serial.print("CONFIG pwm=");
  Serial.println(esp_err_to_name(pwm_result));
  if (pwm_result == ESP_OK) {
    for (size_t run = 0; run < kRuns; ++run) {
      run_case(run);
    }
  }

  cleanup_pwm();
  vQueueDelete(capture_state.queue);
  free(ring_buffer);
  free(destination);
  capture_state.queue = nullptr;
  ring_buffer = nullptr;
  destination = nullptr;
  Serial.println("DONE status=ok");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  parse_pins();
}

void loop() {
  if (Serial.available() > 0 && Serial.read() == '?') {
    run_experiment();
  }
  delay(1);
}
