// E018: suppress PARLIO descriptor cache-error logging, then capture 1 MiB.
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
constexpr size_t kControlSize = 8192;
constexpr size_t kCaptureSize = 1024 * 1024;
constexpr size_t kCaptureRuns = 3;
constexpr uint32_t kDuties[kLaneCount] = {
    16, 48, 80, 112, 144, 176, 208, 240,
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

esp_err_t create_receiver(parlio_rx_unit_handle_t *rx_unit) {
  parlio_rx_unit_config_t config = {};
  config.trans_queue_depth = 1;
  config.max_recv_size = kCaptureSize;
  config.dma_burst_size = 0;
  config.data_width = kLaneCount;
  config.clk_src = PARLIO_CLK_SRC_DEFAULT;
  config.exp_clk_freq_hz = kSampleRateHz;
  config.clk_in_gpio_num = GPIO_NUM_NC;
  config.clk_out_gpio_num = GPIO_NUM_NC;
  config.valid_gpio_num = GPIO_NUM_NC;
  for (size_t lane = 0; lane < PARLIO_RX_UNIT_MAX_DATA_WIDTH; ++lane) {
    config.data_gpio_nums[lane] =
        lane < kLaneCount ? static_cast<gpio_num_t>(pins[lane]) : GPIO_NUM_NC;
  }
  config.flags.io_loop_back = false;
  esp_err_t result = parlio_new_rx_unit(&config, rx_unit);
  if (result == ESP_OK) {
    result = parlio_rx_unit_enable(*rx_unit, true);
  }
  return result;
}

esp_err_t create_delimiter(size_t size,
                           parlio_rx_delimiter_handle_t *delimiter) {
  parlio_rx_soft_delimiter_config_t config = {};
  config.sample_edge = PARLIO_SAMPLE_EDGE_POS;
  config.bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB;
  config.eof_data_len = size;
  return parlio_new_rx_soft_delimiter(&config, delimiter);
}

void print_case(const char *kind, size_t run, size_t size,
                parlio_rx_unit_handle_t rx_unit,
                parlio_rx_delimiter_handle_t delimiter) {
  memset(samples, 0xA5, size);
  esp_err_t sync_result =
      esp_cache_msync(samples, size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  esp_err_t receive_result = ESP_FAIL;
  esp_err_t start_result = ESP_FAIL;
  esp_err_t wait_result = ESP_FAIL;
  esp_err_t stop_result = ESP_FAIL;
  int64_t capture_us = 0;
  int64_t sync_us = 0;

  parlio_receive_config_t receive_config = {};
  receive_config.delimiter = delimiter;
  if (sync_result == ESP_OK) {
    receive_result =
        parlio_rx_unit_receive(rx_unit, samples, size, &receive_config);
  }
  if (receive_result == ESP_OK) {
    const int64_t begin = esp_timer_get_time();
    start_result =
        parlio_rx_soft_delimiter_start_stop(rx_unit, delimiter, true);
    if (start_result == ESP_OK) {
      wait_result = parlio_rx_unit_wait_all_done(rx_unit, 2000);
    }
    capture_us = esp_timer_get_time() - begin;
    stop_result =
        parlio_rx_soft_delimiter_start_stop(rx_unit, delimiter, false);
  }
  if (wait_result == ESP_OK && stop_result == ESP_OK) {
    const int64_t begin = esp_timer_get_time();
    sync_result =
        esp_cache_msync(samples, size, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    sync_us = esp_timer_get_time() - begin;
  }

  esp_err_t result = sync_result;
  if (receive_result != ESP_OK) {
    result = receive_result;
  } else if (start_result != ESP_OK) {
    result = start_result;
  } else if (wait_result != ESP_OK) {
    result = wait_result;
  } else if (stop_result != ESP_OK) {
    result = stop_result;
  }

  uint32_t max_error_ppm = 0;
  size_t min_edges = SIZE_MAX;
  size_t max_edges = 0;
  if (result == ESP_OK) {
    for (size_t lane = 0; lane < kLaneCount; ++lane) {
      const uint8_t mask = 1U << lane;
      size_t high = 0;
      size_t edges = 0;
      for (size_t sample = 0; sample < size; ++sample) {
        high += (samples[sample] & mask) != 0;
        if (sample != 0 &&
            ((samples[sample - 1] ^ samples[sample]) & mask) != 0) {
          ++edges;
        }
      }
      const uint32_t ratio_ppm = high * 1000000ULL / size;
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

  Serial.print("CASE kind=");
  Serial.print(kind);
  Serial.print(" run=");
  Serial.print(run);
  Serial.print(" size=");
  Serial.print(size);
  Serial.print(" result=");
  Serial.print(esp_err_to_name(result));
  Serial.print(" receive=");
  Serial.print(esp_err_to_name(receive_result));
  Serial.print(" start=");
  Serial.print(esp_err_to_name(start_result));
  Serial.print(" wait=");
  Serial.print(esp_err_to_name(wait_result));
  Serial.print(" stop=");
  Serial.print(esp_err_to_name(stop_result));
  Serial.print(" sync=");
  Serial.print(esp_err_to_name(sync_result));
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
}

void run_experiment() {
  Serial.print("# EXP E018 v1 git=");
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
  parlio_rx_unit_handle_t rx_unit = nullptr;
  const esp_err_t receiver_result = create_receiver(&rx_unit);
  Serial.print("CONFIG pwm=");
  Serial.print(esp_err_to_name(pwm_result));
  Serial.print(" receiver=");
  Serial.println(esp_err_to_name(receiver_result));

  parlio_rx_delimiter_handle_t control_delimiter = nullptr;
  parlio_rx_delimiter_handle_t capture_delimiter = nullptr;
  const esp_err_t control_result =
      create_delimiter(kControlSize, &control_delimiter);
  const esp_err_t capture_result =
      create_delimiter(kCaptureSize, &capture_delimiter);
  Serial.print("DELIMITERS control=");
  Serial.print(esp_err_to_name(control_result));
  Serial.print(" capture=");
  Serial.println(esp_err_to_name(capture_result));

  if (pwm_result == ESP_OK && receiver_result == ESP_OK &&
      control_result == ESP_OK && capture_result == ESP_OK) {
    print_case("control", 0, kControlSize, rx_unit, control_delimiter);
    esp_log_level_set("cache", ESP_LOG_NONE);
    Serial.println("LOG tag=cache level=none");
    for (size_t run = 0; run < kCaptureRuns; ++run) {
      print_case("suppressed", run, kCaptureSize, rx_unit,
                 capture_delimiter);
    }
  }

  if (rx_unit != nullptr) {
    parlio_rx_unit_disable(rx_unit);
  }
  if (control_delimiter != nullptr) {
    parlio_del_rx_delimiter(control_delimiter);
  }
  if (capture_delimiter != nullptr) {
    parlio_del_rx_delimiter(capture_delimiter);
  }
  if (rx_unit != nullptr) {
    parlio_del_rx_unit(rx_unit);
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
