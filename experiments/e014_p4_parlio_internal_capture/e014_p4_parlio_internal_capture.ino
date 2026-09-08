// E014: capture eight LEDC outputs through PARLIO RX on the same GPIOs.
// No external wiring is used; PARLIO io_loop_back enables the GPIO input path.
// Plan and report: README.ja.md

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/parlio_rx.h>
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

DMA_ATTR uint8_t samples[kSampleCount];
int pins[kLaneCount] = {};
parlio_rx_unit_handle_t rx_unit = nullptr;
parlio_rx_delimiter_handle_t delimiter = nullptr;
esp_err_t init_result = ESP_OK;

bool parse_pins() {
  const char *cursor = PARLIO_PINS;
  for (size_t lane = 0; lane < kLaneCount; ++lane) {
    char *end = nullptr;
    long value = strtol(cursor, &end, 10);
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
    const uint32_t duty = 32U * (lane + 1U);
    if (!ledcWrite(pins[lane], duty)) {
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
  rx_config.flags.io_loop_back = true;

  esp_err_t result = parlio_new_rx_unit(&rx_config, &rx_unit);
  if (result != ESP_OK) {
    return result;
  }

  parlio_rx_soft_delimiter_config_t delimiter_config = {};
  delimiter_config.sample_edge = PARLIO_SAMPLE_EDGE_POS;
  delimiter_config.bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB;
  delimiter_config.eof_data_len = kSampleCount;
  delimiter_config.timeout_ticks = 0;
  result = parlio_new_rx_soft_delimiter(&delimiter_config, &delimiter);
  if (result != ESP_OK) {
    return result;
  }
  return parlio_rx_unit_enable(rx_unit, true);
}

esp_err_t capture_once() {
  parlio_receive_config_t receive_config = {};
  receive_config.delimiter = delimiter;

  esp_err_t result =
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
  return result == ESP_OK ? stop_result : result;
}

void print_banner() {
  Serial.print("# EXP E014 v1 git=");
  Serial.print(BANNER_GIT);
  Serial.print(" probe=esp32p4_parlio target=internal build=");
  Serial.println(__DATE__ " " __TIME__);
}

void print_configuration() {
  Serial.print("CONFIG pwm_hz=");
  Serial.print(kPwmFrequencyHz);
  Serial.print(" sample_hz=");
  Serial.print(kSampleRateHz);
  Serial.print(" samples=");
  Serial.print(kSampleCount);
  Serial.print(" pins=");
  uint64_t gpio_mask = 0;
  for (size_t lane = 0; lane < kLaneCount; ++lane) {
    if (lane != 0) {
      Serial.print(',');
    }
    Serial.print(pins[lane]);
    gpio_mask |= 1ULL << pins[lane];
  }
  Serial.println();
  Serial.flush();
  gpio_dump_io_configuration(stdout, gpio_mask);
}

void run_capture() {
  print_banner();
  Serial.print("INIT result=");
  Serial.println(esp_err_to_name(init_result));
  if (init_result != ESP_OK) {
    Serial.println("DONE result=FAIL");
    return;
  }

  print_configuration();
  for (size_t run = 0; run < kCaptureRuns; ++run) {
    const esp_err_t result = capture_once();
    Serial.print("CAPTURE run=");
    Serial.print(run);
    Serial.print(" result=");
    Serial.println(esp_err_to_name(result));
    if (result != ESP_OK) {
      Serial.println("DONE result=FAIL");
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
      const size_t low = kSampleCount - high;
      const uint32_t duty = 32U * (lane + 1U);
      const uint32_t ratio_ppm =
          static_cast<uint32_t>((high * 1000000ULL) / kSampleCount);
      Serial.print("LANE run=");
      Serial.print(run);
      Serial.print(" lane=");
      Serial.print(lane);
      Serial.print(" pin=");
      Serial.print(pins[lane]);
      Serial.print(" duty=");
      Serial.print(duty);
      Serial.print(" high=");
      Serial.print(high);
      Serial.print(" low=");
      Serial.print(low);
      Serial.print(" edges=");
      Serial.print(edges);
      Serial.print(" ratio_ppm=");
      Serial.println(ratio_ppm);
    }
  }
  Serial.println("DONE result=PASS");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  if (!parse_pins()) {
    init_result = ESP_ERR_INVALID_ARG;
    return;
  }
  init_result = configure_pwm();
  if (init_result == ESP_OK) {
    delay(10);
    init_result = configure_parlio();
  }
}

void loop() {
  if (Serial.available() > 0 && Serial.read() == '?') {
    run_capture();
  }
  delay(1);
}
