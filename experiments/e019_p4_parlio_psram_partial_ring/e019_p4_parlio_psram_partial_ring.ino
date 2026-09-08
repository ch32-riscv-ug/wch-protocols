// E019: one full turn of a 1 MiB direct PSRAM PARLIO partial-RX ring.
// Plan and report: README.ja.md

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/parlio_rx.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_memory_utils.h>
#include <esp_private/esp_cache_private.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifndef BANNER_GIT
#define BANNER_GIT "unknown"
#endif

#ifndef PARLIO_PINS
#define PARLIO_PINS "2,3,4,5,6,7,8,9"
#endif

namespace {

constexpr size_t kLaneCount = 8;
constexpr uint32_t kPwmFrequencyHz = 100000;
constexpr uint8_t kPwmResolutionBits = 8;
constexpr uint32_t kSampleRateHz = 8000000;
constexpr size_t kCaptureSize = 1024 * 1024;
constexpr size_t kDelimiterSize = 65408;
constexpr size_t kCaptureRuns = 3;
constexpr uint32_t kDuties[kLaneCount] = {
    16, 48, 80, 112, 144, 176, 208, 240,
};

struct CaptureState {
  TaskHandle_t waiter;
  volatile size_t bytes;
  volatile size_t callbacks;
  volatile bool reached;
};

int pins[kLaneCount] = {};
bool ledc_attached[kLaneCount] = {};
uint8_t *samples = nullptr;

bool parse_pins() {
  const char *cursor = PARLIO_PINS;
  for (size_t lane = 0; lane < kLaneCount; ++lane) {
    char *end = nullptr;
    const long value = strtol(cursor, &end, 10);
    if (end == cursor || value < 0 || value >= GPIO_NUM_MAX) {
      return false;
    }
    pins[lane] = static_cast<int>(value);
    if (lane + 1 < kLaneCount) {
      if (*end != ',') {
        return false;
      }
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
                           lane)) {
      return ESP_FAIL;
    }
    ledc_attached[lane] = true;
    if (!ledcWrite(pins[lane], kDuties[lane])) {
      return ESP_FAIL;
    }
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
  state->bytes += event->recv_bytes;
  ++state->callbacks;
  if (!state->reached && state->bytes >= kCaptureSize) {
    state->reached = true;
    BaseType_t high_task_woken = pdFALSE;
    vTaskNotifyGiveFromISR(state->waiter, &high_task_woken);
    return high_task_woken == pdTRUE;
  }
  return false;
}

esp_err_t create_receiver(CaptureState *state,
                          parlio_rx_unit_handle_t *rx_unit,
                          parlio_rx_delimiter_handle_t *delimiter) {
  parlio_rx_unit_config_t unit_config = {};
  unit_config.trans_queue_depth = 1;
  unit_config.max_recv_size = kCaptureSize;
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
  if (result != ESP_OK) {
    return result;
  }
  parlio_rx_event_callbacks_t callbacks = {};
  callbacks.on_partial_receive = on_partial_receive;
  result = parlio_rx_unit_register_event_callbacks(*rx_unit, &callbacks, state);
  if (result != ESP_OK) {
    return result;
  }

  parlio_rx_soft_delimiter_config_t delimiter_config = {};
  delimiter_config.sample_edge = PARLIO_SAMPLE_EDGE_POS;
  delimiter_config.bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB;
  delimiter_config.eof_data_len = kDelimiterSize;
  result = parlio_new_rx_soft_delimiter(&delimiter_config, delimiter);
  if (result != ESP_OK) {
    return result;
  }
  return parlio_rx_unit_enable(*rx_unit, true);
}

void destroy_receiver(parlio_rx_unit_handle_t rx_unit,
                      parlio_rx_delimiter_handle_t delimiter, bool enabled) {
  if (enabled && rx_unit != nullptr) {
    parlio_rx_unit_disable(rx_unit);
  }
  if (delimiter != nullptr) {
    parlio_del_rx_delimiter(delimiter);
  }
  if (rx_unit != nullptr) {
    parlio_del_rx_unit(rx_unit);
  }
}

void run_case(size_t run) {
  CaptureState state = {
      .waiter = xTaskGetCurrentTaskHandle(),
      .bytes = 0,
      .callbacks = 0,
      .reached = false,
  };
  ulTaskNotifyTake(pdTRUE, 0);

  parlio_rx_unit_handle_t rx_unit = nullptr;
  parlio_rx_delimiter_handle_t delimiter = nullptr;
  const esp_err_t config_result =
      create_receiver(&state, &rx_unit, &delimiter);
  bool enabled = config_result == ESP_OK;

  memset(samples, 0xA5, kCaptureSize);
  esp_err_t sync_result = esp_cache_msync(
      samples, kCaptureSize, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  esp_err_t receive_result = ESP_FAIL;
  esp_err_t start_result = ESP_FAIL;
  uint32_t notified = 0;
  esp_err_t stop_result = ESP_FAIL;
  esp_err_t disable_result = ESP_FAIL;
  int64_t capture_us = 0;
  int64_t sync_us = 0;

  parlio_receive_config_t receive_config = {};
  receive_config.delimiter = delimiter;
  receive_config.flags.partial_rx_en = true;
  receive_config.flags.indirect_mount = false;
  if (config_result == ESP_OK && sync_result == ESP_OK) {
    receive_result = parlio_rx_unit_receive(rx_unit, samples, kCaptureSize,
                                            &receive_config);
  }
  const int64_t capture_begin = esp_timer_get_time();
  if (receive_result == ESP_OK) {
    start_result =
        parlio_rx_soft_delimiter_start_stop(rx_unit, delimiter, true);
  }
  if (start_result == ESP_OK) {
    notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));
  }
  if (notified != 0) {
    stop_result =
        parlio_rx_soft_delimiter_start_stop(rx_unit, delimiter, false);
  }
  capture_us = esp_timer_get_time() - capture_begin;
  if (enabled) {
    disable_result = parlio_rx_unit_disable(rx_unit);
    if (disable_result == ESP_OK) {
      enabled = false;
    }
  }
  if (disable_result == ESP_OK) {
    const int64_t sync_begin = esp_timer_get_time();
    sync_result = esp_cache_msync(
        samples, kCaptureSize, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    sync_us = esp_timer_get_time() - sync_begin;
  }

  esp_err_t result = config_result;
  if (result == ESP_OK) result = receive_result;
  if (result == ESP_OK) result = start_result;
  if (result == ESP_OK && notified == 0) result = ESP_ERR_TIMEOUT;
  if (result == ESP_OK) result = stop_result;
  if (result == ESP_OK) result = disable_result;
  if (result == ESP_OK) result = sync_result;

  uint32_t max_error_ppm = 0;
  size_t min_edges = SIZE_MAX;
  size_t max_edges = 0;
  if (result == ESP_OK) {
    for (size_t lane = 0; lane < kLaneCount; ++lane) {
      const uint8_t mask = 1U << lane;
      size_t high = 0;
      size_t edges = 0;
      for (size_t sample = 0; sample < kCaptureSize; ++sample) {
        high += (samples[sample] & mask) != 0;
        if (sample != 0 &&
            ((samples[sample - 1] ^ samples[sample]) & mask) != 0) {
          ++edges;
        }
      }
      const uint32_t ratio_ppm = high * 1000000ULL / kCaptureSize;
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

  const size_t bytes = state.bytes;
  const size_t callbacks = state.callbacks;
  Serial.print("CASE run=");
  Serial.print(run);
  Serial.print(" result=");
  Serial.print(esp_err_to_name(result));
  Serial.print(" config=");
  Serial.print(esp_err_to_name(config_result));
  Serial.print(" receive=");
  Serial.print(esp_err_to_name(receive_result));
  Serial.print(" start=");
  Serial.print(esp_err_to_name(start_result));
  Serial.print(" notified=");
  Serial.print(notified);
  Serial.print(" stop=");
  Serial.print(esp_err_to_name(stop_result));
  Serial.print(" disable=");
  Serial.print(esp_err_to_name(disable_result));
  Serial.print(" sync=");
  Serial.print(esp_err_to_name(sync_result));
  Serial.print(" callbacks=");
  Serial.print(callbacks);
  Serial.print(" bytes=");
  Serial.print(bytes);
  Serial.print(" extra_bytes=");
  Serial.print(bytes > kCaptureSize ? bytes - kCaptureSize : 0);
  Serial.print(" capture_us=");
  Serial.print(capture_us);
  Serial.print(" sync_us=");
  Serial.print(sync_us);
  Serial.print(" max_error_ppm=");
  Serial.print(max_error_ppm);
  Serial.print(" min_edges=");
  Serial.print(min_edges);
  Serial.print(" max_edges=");
  Serial.println(max_edges);

  destroy_receiver(rx_unit, delimiter, enabled);
}

void run_experiment() {
  Serial.print("# EXP E019 v1 git=");
  Serial.print(BANNER_GIT);
  Serial.print(" probe=esp32p4_parlio target=internal build=");
  Serial.println(__DATE__ " " __TIME__);

  size_t alignment = 0;
  const esp_err_t alignment_result =
      esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &alignment);
  Serial.print("ENV psram_found=");
  Serial.print(psramFound());
  Serial.print(" psram_size=");
  Serial.print(ESP.getPsramSize());
  Serial.print(" alignment_result=");
  Serial.print(esp_err_to_name(alignment_result));
  Serial.print(" ext_align=");
  Serial.println(alignment);
  if (!psramFound() || alignment_result != ESP_OK) {
    Serial.println("DONE status=environment-failed");
    return;
  }

  samples = static_cast<uint8_t *>(heap_caps_aligned_alloc(
      alignment, kCaptureSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  Serial.print("BUFFER ptr=");
  Serial.print(reinterpret_cast<uintptr_t>(samples), HEX);
  Serial.print(" size=");
  Serial.print(kCaptureSize);
  Serial.print(" aligned=");
  Serial.print(samples != nullptr &&
               reinterpret_cast<uintptr_t>(samples) % alignment == 0);
  Serial.print(" external=");
  Serial.print(samples != nullptr && esp_ptr_external_ram(samples));
  Serial.print(" dma_ext=");
  Serial.println(samples != nullptr && esp_ptr_dma_ext_capable(samples));
  if (samples == nullptr) {
    Serial.println("DONE status=alloc-failed");
    return;
  }

  const esp_err_t pwm_result = configure_pwm();
  Serial.print("PWM result=");
  Serial.println(esp_err_to_name(pwm_result));
  esp_log_level_set("cache", ESP_LOG_NONE);
  Serial.println("LOG tag=cache level=none");
  if (pwm_result == ESP_OK) {
    for (size_t run = 0; run < kCaptureRuns; ++run) {
      run_case(run);
    }
  }
  cleanup_pwm();
  free(samples);
  samples = nullptr;
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
