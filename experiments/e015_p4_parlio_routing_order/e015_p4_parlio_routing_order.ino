// E015: compare two initialization orders for LEDC + PARLIO RX on the same GPIOs.
// Plan and report: README.ja.md

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/parlio_rx.h>
#include <esp_cache.h>
#include <esp_err.h>

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
constexpr size_t kSampleCount = 8192;
constexpr size_t kCaptureRuns = 3;
constexpr uint32_t kDuties[kLaneCount] = {
    16, 48, 80, 112, 144, 176, 208, 240,
};

DMA_ATTR __attribute__((aligned(64))) uint8_t samples[kSampleCount];
int pins[kLaneCount] = {};
parlio_rx_unit_handle_t rx_unit = nullptr;
parlio_rx_delimiter_handle_t delimiter = nullptr;
bool rx_enabled = false;
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

esp_err_t configure_parlio() {
  parlio_rx_unit_config_t rx_config = {};
  rx_config.trans_queue_depth = 1;
  rx_config.max_recv_size = kSampleCount;
  rx_config.dma_burst_size = 64;
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

  esp_err_t result = parlio_new_rx_unit(&rx_config, &rx_unit);
  if (result != ESP_OK) {
    return result;
  }

  parlio_rx_soft_delimiter_config_t delimiter_config = {};
  delimiter_config.sample_edge = PARLIO_SAMPLE_EDGE_POS;
  delimiter_config.bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB;
  delimiter_config.eof_data_len = kSampleCount;
  result = parlio_new_rx_soft_delimiter(&delimiter_config, &delimiter);
  if (result != ESP_OK) {
    return result;
  }
  result = parlio_rx_unit_enable(rx_unit, true);
  rx_enabled = result == ESP_OK;
  return result;
}

void cleanup() {
  if (rx_enabled) {
    parlio_rx_unit_disable(rx_unit);
    rx_enabled = false;
  }
  if (delimiter != nullptr) {
    parlio_del_rx_delimiter(delimiter);
    delimiter = nullptr;
  }
  if (rx_unit != nullptr) {
    parlio_del_rx_unit(rx_unit);
    rx_unit = nullptr;
  }
  for (size_t lane = 0; lane < kLaneCount; ++lane) {
    if (ledc_attached[lane]) {
      ledcDetach(pins[lane]);
      ledc_attached[lane] = false;
    }
    gpio_reset_pin(static_cast<gpio_num_t>(pins[lane]));
  }
}

void print_mapping(const char *variant, const char *stage) {
  Serial.print("DUMP variant=");
  Serial.print(variant);
  Serial.print(" stage=");
  Serial.println(stage);
  for (size_t lane = 0; lane < kLaneCount; ++lane) {
    gpio_io_config_t config = {};
    const esp_err_t result =
        gpio_get_io_config(static_cast<gpio_num_t>(pins[lane]), &config);
    Serial.print("MAP variant=");
    Serial.print(variant);
    Serial.print(" stage=");
    Serial.print(stage);
    Serial.print(" lane=");
    Serial.print(lane);
    Serial.print(" pin=");
    Serial.print(pins[lane]);
    Serial.print(" result=");
    Serial.print(esp_err_to_name(result));
    Serial.print(" sig_out=");
    Serial.print(config.sig_out);
    Serial.print(" input_en=");
    Serial.print(config.ie);
    Serial.print(" output_en=");
    Serial.print(config.oe);
    Serial.print(" oe_periph=");
    Serial.println(config.oe_ctrl_by_periph);
  }
  Serial.flush();
  uint64_t gpio_mask = 0;
  for (size_t lane = 0; lane < kLaneCount; ++lane) {
    gpio_mask |= 1ULL << pins[lane];
  }
  gpio_dump_io_configuration(stdout, gpio_mask);
  Serial.print("DUMP_DONE variant=");
  Serial.print(variant);
  Serial.print(" stage=");
  Serial.println(stage);
}

esp_err_t capture_once() {
  memset(samples, 0xA5, sizeof(samples));
  esp_err_t result = esp_cache_msync(
      samples, sizeof(samples), ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  if (result != ESP_OK) {
    return result;
  }

  parlio_receive_config_t receive_config = {};
  receive_config.delimiter = delimiter;
  result =
      parlio_rx_unit_receive(rx_unit, samples, sizeof(samples), &receive_config);
  if (result != ESP_OK) {
    return result;
  }
  result = parlio_rx_soft_delimiter_start_stop(rx_unit, delimiter, true);
  if (result != ESP_OK) {
    return result;
  }
  result = parlio_rx_unit_wait_all_done(rx_unit, 1000);
  const esp_err_t stop_result =
      parlio_rx_soft_delimiter_start_stop(rx_unit, delimiter, false);
  if (result != ESP_OK) {
    return result;
  }
  if (stop_result != ESP_OK) {
    return stop_result;
  }
  return esp_cache_msync(samples, sizeof(samples),
                         ESP_CACHE_MSYNC_FLAG_DIR_M2C);
}

void print_capture(const char *variant, size_t run) {
  const esp_err_t result = capture_once();
  Serial.print("CAPTURE variant=");
  Serial.print(variant);
  Serial.print(" run=");
  Serial.print(run);
  Serial.print(" result=");
  Serial.println(esp_err_to_name(result));
  if (result != ESP_OK) {
    return;
  }

  for (size_t lane = 0; lane < kLaneCount; ++lane) {
    const uint8_t mask = 1U << lane;
    size_t high = 0;
    size_t edges = 0;
    for (size_t sample = 0; sample < kSampleCount; ++sample) {
      high += (samples[sample] & mask) != 0;
      if (sample != 0 &&
          ((samples[sample - 1] ^ samples[sample]) & mask) != 0) {
        ++edges;
      }
    }
    Serial.print("LANE variant=");
    Serial.print(variant);
    Serial.print(" run=");
    Serial.print(run);
    Serial.print(" lane=");
    Serial.print(lane);
    Serial.print(" pin=");
    Serial.print(pins[lane]);
    Serial.print(" duty=");
    Serial.print(kDuties[lane]);
    Serial.print(" high=");
    Serial.print(high);
    Serial.print(" low=");
    Serial.print(kSampleCount - high);
    Serial.print(" edges=");
    Serial.print(edges);
    Serial.print(" ratio_ppm=");
    Serial.println(static_cast<uint32_t>(
        high * 1000000ULL / kSampleCount));
  }
}

void run_variant(const char *name, bool ledc_first) {
  cleanup();
  Serial.print("VARIANT name=");
  Serial.println(name);

  esp_err_t first_result = ledc_first ? configure_pwm() : configure_parlio();
  Serial.print("STAGE variant=");
  Serial.print(name);
  Serial.print(" stage=first result=");
  Serial.println(esp_err_to_name(first_result));
  print_mapping(name, "first");

  esp_err_t second_result = ESP_FAIL;
  if (first_result == ESP_OK) {
    second_result = ledc_first ? configure_parlio() : configure_pwm();
  }
  Serial.print("STAGE variant=");
  Serial.print(name);
  Serial.print(" stage=second result=");
  Serial.println(esp_err_to_name(second_result));
  print_mapping(name, "final");

  for (size_t lane = 0; lane < kLaneCount; ++lane) {
    Serial.print("PWM variant=");
    Serial.print(name);
    Serial.print(" lane=");
    Serial.print(lane);
    Serial.print(" actual_hz=");
    Serial.println(ledcReadFreq(pins[lane]));
  }

  if (first_result == ESP_OK && second_result == ESP_OK) {
    for (size_t run = 0; run < kCaptureRuns; ++run) {
      print_capture(name, run);
    }
  }
  Serial.print("VARIANT_DONE name=");
  Serial.println(name);
}

void run_experiment() {
  Serial.print("# EXP E015 v1 git=");
  Serial.print(BANNER_GIT);
  Serial.print(" probe=esp32p4_parlio target=internal build=");
  Serial.println(__DATE__ " " __TIME__);
  run_variant("ledc-first", true);
  run_variant("parlio-first", false);
  cleanup();
  Serial.println("DONE");
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
