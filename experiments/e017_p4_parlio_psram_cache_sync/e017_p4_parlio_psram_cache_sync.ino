// E017: PSRAM cache-sync boundary of finite PARLIO RX transactions.
// Plan and report: README.ja.md

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/parlio_rx.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <esp_private/esp_cache_private.h>

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
constexpr size_t kCaptureRuns = 3;
constexpr size_t kBurstCount = 3;
constexpr size_t kSizeCount = 5;
constexpr size_t kBursts[kBurstCount] = {0, 64, 128};
constexpr size_t kSizes[kSizeCount] = {3968, 4096, 7936, 8064, 8192};
constexpr size_t kMaxCaptureSize = 8192;
constexpr uint32_t kDuties[kLaneCount] = {
    16, 48, 80, 112, 144, 176, 208, 240,
};

int pins[kLaneCount] = {};
uint8_t *samples = nullptr;
bool ledc_attached[kLaneCount] = {};

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
  for (size_t left = 0; left < kLaneCount; ++left) {
    for (size_t right = left + 1; right < kLaneCount; ++right) {
      if (pins[left] == pins[right]) {
        return false;
      }
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

esp_err_t create_receiver(size_t burst, size_t capture_size,
                          parlio_rx_unit_handle_t *rx_unit,
                          parlio_rx_delimiter_handle_t *delimiter) {
  parlio_rx_unit_config_t rx_config = {};
  rx_config.trans_queue_depth = 1;
  rx_config.max_recv_size = capture_size;
  rx_config.dma_burst_size = burst;
  rx_config.data_width = kLaneCount;
  rx_config.clk_src = PARLIO_CLK_SRC_DEFAULT;
  rx_config.exp_clk_freq_hz = kSampleRateHz;
  rx_config.clk_in_gpio_num = GPIO_NUM_NC;
  rx_config.clk_out_gpio_num = GPIO_NUM_NC;
  rx_config.valid_gpio_num = GPIO_NUM_NC;
  for (size_t lane = 0; lane < PARLIO_RX_UNIT_MAX_DATA_WIDTH; ++lane) {
    rx_config.data_gpio_nums[lane] =
        lane < kLaneCount ? static_cast<gpio_num_t>(pins[lane]) : GPIO_NUM_NC;
  }
  rx_config.flags.io_loop_back = false;

  esp_err_t result = parlio_new_rx_unit(&rx_config, rx_unit);
  if (result != ESP_OK) {
    return result;
  }
  parlio_rx_soft_delimiter_config_t delimiter_config = {};
  delimiter_config.sample_edge = PARLIO_SAMPLE_EDGE_POS;
  delimiter_config.bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB;
  delimiter_config.eof_data_len = capture_size;
  result = parlio_new_rx_soft_delimiter(&delimiter_config, delimiter);
  if (result != ESP_OK) {
    return result;
  }
  return parlio_rx_unit_enable(*rx_unit, true);
}

void destroy_receiver(parlio_rx_unit_handle_t rx_unit,
                      parlio_rx_delimiter_handle_t delimiter,
                      bool enabled) {
  if (enabled) {
    parlio_rx_unit_disable(rx_unit);
  }
  if (delimiter != nullptr) {
    parlio_del_rx_delimiter(delimiter);
  }
  if (rx_unit != nullptr) {
    parlio_del_rx_unit(rx_unit);
  }
}

void print_failed_cases(size_t burst, size_t capture_size,
                        esp_err_t config_result) {
  for (size_t run = 0; run < kCaptureRuns; ++run) {
    Serial.print("CASE burst=");
    Serial.print(burst);
    Serial.print(" size=");
    Serial.print(capture_size);
    Serial.print(" run=");
    Serial.print(run);
    Serial.print(" result=");
    Serial.print(esp_err_to_name(config_result));
    Serial.println(" receive=NA start=NA wait=NA stop=NA sync=NA max_error_ppm=0 min_edges=0 max_edges=0");
  }
}

void capture_case(size_t burst, size_t capture_size, size_t run,
                  parlio_rx_unit_handle_t rx_unit,
                  parlio_rx_delimiter_handle_t delimiter) {
  memset(samples, 0xA5, capture_size);
  esp_err_t sync_result = esp_cache_msync(
      samples, capture_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  esp_err_t receive_result = ESP_FAIL;
  esp_err_t start_result = ESP_FAIL;
  esp_err_t wait_result = ESP_FAIL;
  esp_err_t stop_result = ESP_FAIL;

  parlio_receive_config_t receive_config = {};
  receive_config.delimiter = delimiter;
  if (sync_result == ESP_OK) {
    receive_result = parlio_rx_unit_receive(rx_unit, samples, capture_size,
                                            &receive_config);
  }
  if (receive_result == ESP_OK) {
    start_result =
        parlio_rx_soft_delimiter_start_stop(rx_unit, delimiter, true);
  }
  if (start_result == ESP_OK) {
    wait_result = parlio_rx_unit_wait_all_done(rx_unit, 1000);
    stop_result =
        parlio_rx_soft_delimiter_start_stop(rx_unit, delimiter, false);
  }
  if (wait_result == ESP_OK && stop_result == ESP_OK) {
    sync_result = esp_cache_msync(
        samples, capture_size, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
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
      for (size_t sample = 0; sample < capture_size; ++sample) {
        high += (samples[sample] & mask) != 0;
        if (sample != 0 &&
            ((samples[sample - 1] ^ samples[sample]) & mask) != 0) {
          ++edges;
        }
      }
      const uint32_t ratio_ppm =
          static_cast<uint32_t>(high * 1000000ULL / capture_size);
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

  Serial.print("CASE burst=");
  Serial.print(burst);
  Serial.print(" size=");
  Serial.print(capture_size);
  Serial.print(" run=");
  Serial.print(run);
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
  Serial.print(" max_error_ppm=");
  Serial.print(max_error_ppm);
  Serial.print(" min_edges=");
  Serial.print(min_edges);
  Serial.print(" max_edges=");
  Serial.println(max_edges);
}

void run_profile(size_t burst, size_t capture_size) {
  parlio_rx_unit_handle_t rx_unit = nullptr;
  parlio_rx_delimiter_handle_t delimiter = nullptr;
  const esp_err_t config_result =
      create_receiver(burst, capture_size, &rx_unit, &delimiter);
  Serial.print("PROFILE burst=");
  Serial.print(burst);
  Serial.print(" size=");
  Serial.print(capture_size);
  Serial.print(" result=");
  Serial.println(esp_err_to_name(config_result));
  if (config_result != ESP_OK) {
    print_failed_cases(burst, capture_size, config_result);
    destroy_receiver(rx_unit, delimiter, false);
    return;
  }
  for (size_t run = 0; run < kCaptureRuns; ++run) {
    capture_case(burst, capture_size, run, rx_unit, delimiter);
  }
  destroy_receiver(rx_unit, delimiter, true);
}

void run_experiment() {
  Serial.print("# EXP E017 v1 git=");
  Serial.print(BANNER_GIT);
  Serial.print(" probe=esp32p4_parlio target=internal build=");
  Serial.println(__DATE__ " " __TIME__);

  size_t external_alignment = 0;
  const esp_err_t alignment_result =
      esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &external_alignment);
  Serial.print("ENV psram_found=");
  Serial.print(psramFound());
  Serial.print(" psram_size=");
  Serial.print(ESP.getPsramSize());
  Serial.print(" alignment_result=");
  Serial.print(esp_err_to_name(alignment_result));
  Serial.print(" ext_align=");
  Serial.println(external_alignment);
  if (!psramFound() || alignment_result != ESP_OK) {
    Serial.println("DONE status=environment-failed");
    return;
  }

  samples = static_cast<uint8_t *>(heap_caps_aligned_alloc(
      external_alignment, kMaxCaptureSize,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  Serial.print("BUFFER ptr=");
  Serial.print(reinterpret_cast<uintptr_t>(samples), HEX);
  Serial.print(" aligned=");
  Serial.print(samples != nullptr &&
                       reinterpret_cast<uintptr_t>(samples) %
                               external_alignment ==
                           0);
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
  if (pwm_result == ESP_OK) {
    for (size_t burst_index = 0; burst_index < kBurstCount; ++burst_index) {
      for (size_t size_index = 0; size_index < kSizeCount; ++size_index) {
        run_profile(kBursts[burst_index], kSizes[size_index]);
      }
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
