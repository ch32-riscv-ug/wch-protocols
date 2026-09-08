// E016: finite PARLIO RX directly into ESP32-P4 PSRAM.
// Plan and report: README.ja.md

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/parlio_rx.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <esp_private/esp_cache_private.h>
#include <esp_system.h>

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
constexpr size_t kDmaBurstSize = 64;
constexpr uint32_t kDuties[kLaneCount] = {
    16, 48, 80, 112, 144, 176, 208, 240,
};

int pins[kLaneCount] = {};
uint8_t *internal_samples = nullptr;
uint8_t *psram_samples = nullptr;
parlio_rx_unit_handle_t rx_unit = nullptr;
parlio_rx_delimiter_handle_t delimiter = nullptr;
bool rx_enabled = false;
bool ledc_attached[kLaneCount] = {};
size_t internal_alignment = 0;
size_t external_alignment = 0;

size_t max_size(size_t left, size_t right) {
  return left > right ? left : right;
}

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
  rx_config.dma_burst_size = kDmaBurstSize;
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
  free(internal_samples);
  internal_samples = nullptr;
  free(psram_samples);
  psram_samples = nullptr;
}

void print_buffer(const char *kind, uint8_t *buffer, size_t alignment) {
  Serial.print("BUFFER kind=");
  Serial.print(kind);
  Serial.print(" ptr=");
  Serial.print(reinterpret_cast<uintptr_t>(buffer), HEX);
  Serial.print(" size=");
  Serial.print(kSampleCount);
  Serial.print(" alignment=");
  Serial.print(alignment);
  Serial.print(" aligned=");
  Serial.print(buffer != nullptr &&
                       reinterpret_cast<uintptr_t>(buffer) % alignment == 0);
  Serial.print(" internal=");
  Serial.print(buffer != nullptr && esp_ptr_internal(buffer));
  Serial.print(" external=");
  Serial.print(buffer != nullptr && esp_ptr_external_ram(buffer));
  Serial.print(" dma=");
  Serial.print(buffer != nullptr && esp_ptr_dma_capable(buffer));
  Serial.print(" dma_ext=");
  Serial.println(buffer != nullptr && esp_ptr_dma_ext_capable(buffer));
}

esp_err_t capture_once(uint8_t *buffer, esp_err_t *receive_result,
                       esp_err_t *start_result, esp_err_t *wait_result,
                       esp_err_t *stop_result, esp_err_t *sync_result) {
  memset(buffer, 0xA5, kSampleCount);
  esp_err_t result = esp_cache_msync(
      buffer, kSampleCount, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  if (result != ESP_OK) {
    *sync_result = result;
    return result;
  }

  parlio_receive_config_t receive_config = {};
  receive_config.delimiter = delimiter;
  receive_config.flags.partial_rx_en = false;
  receive_config.flags.indirect_mount = false;
  *receive_result = parlio_rx_unit_receive(rx_unit, buffer, kSampleCount,
                                           &receive_config);
  if (*receive_result != ESP_OK) {
    return *receive_result;
  }
  *start_result =
      parlio_rx_soft_delimiter_start_stop(rx_unit, delimiter, true);
  if (*start_result != ESP_OK) {
    return *start_result;
  }
  *wait_result = parlio_rx_unit_wait_all_done(rx_unit, 1000);
  *stop_result =
      parlio_rx_soft_delimiter_start_stop(rx_unit, delimiter, false);
  if (*wait_result != ESP_OK) {
    return *wait_result;
  }
  if (*stop_result != ESP_OK) {
    return *stop_result;
  }
  *sync_result = esp_cache_msync(
      buffer, kSampleCount, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
  return *sync_result;
}

void run_capture(const char *kind, uint8_t *buffer, size_t run) {
  esp_err_t receive_result = ESP_FAIL;
  esp_err_t start_result = ESP_FAIL;
  esp_err_t wait_result = ESP_FAIL;
  esp_err_t stop_result = ESP_FAIL;
  esp_err_t sync_result = ESP_FAIL;
  const esp_err_t result =
      capture_once(buffer, &receive_result, &start_result, &wait_result,
                   &stop_result, &sync_result);

  Serial.print("CAPTURE kind=");
  Serial.print(kind);
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
  Serial.println(esp_err_to_name(sync_result));
  if (result != ESP_OK) {
    return;
  }

  for (size_t lane = 0; lane < kLaneCount; ++lane) {
    const uint8_t mask = 1U << lane;
    size_t high = 0;
    size_t edges = 0;
    for (size_t sample = 0; sample < kSampleCount; ++sample) {
      high += (buffer[sample] & mask) != 0;
      if (sample != 0 &&
          ((buffer[sample - 1] ^ buffer[sample]) & mask) != 0) {
        ++edges;
      }
    }
    Serial.print("LANE kind=");
    Serial.print(kind);
    Serial.print(" run=");
    Serial.print(run);
    Serial.print(" lane=");
    Serial.print(lane);
    Serial.print(" duty=");
    Serial.print(kDuties[lane]);
    Serial.print(" high=");
    Serial.print(high);
    Serial.print(" low=");
    Serial.print(kSampleCount - high);
    Serial.print(" edges=");
    Serial.print(edges);
    Serial.print(" ratio_ppm=");
    Serial.println(static_cast<uint32_t>(high * 1000000ULL / kSampleCount));
  }
}

void run_experiment() {
  Serial.print("# EXP E016 v1 git=");
  Serial.print(BANNER_GIT);
  Serial.print(" probe=esp32p4_parlio target=internal build=");
  Serial.println(__DATE__ " " __TIME__);

  const bool psram_found = psramFound();
  const esp_err_t int_align_result =
      esp_cache_get_alignment(MALLOC_CAP_INTERNAL, &internal_alignment);
  const esp_err_t ext_align_result =
      esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &external_alignment);
  internal_alignment = max_size(internal_alignment, kDmaBurstSize);
  external_alignment = max_size(external_alignment, kDmaBurstSize);

  Serial.print("ENV idf=");
  Serial.print(esp_get_idf_version());
  Serial.print(" psram_found=");
  Serial.print(psram_found);
  Serial.print(" psram_size=");
  Serial.print(ESP.getPsramSize());
  Serial.print(" psram_free=");
  Serial.print(ESP.getFreePsram());
  Serial.print(" int_align_result=");
  Serial.print(esp_err_to_name(int_align_result));
  Serial.print(" int_align=");
  Serial.print(internal_alignment);
  Serial.print(" ext_align_result=");
  Serial.print(esp_err_to_name(ext_align_result));
  Serial.print(" ext_align=");
  Serial.println(external_alignment);

  if (!psram_found || ESP.getPsramSize() == 0) {
    Serial.println("DONE status=no-psram");
    return;
  }
  if (int_align_result != ESP_OK || ext_align_result != ESP_OK ||
      internal_alignment == 0 || external_alignment == 0) {
    Serial.println("DONE status=alignment-failed");
    return;
  }

  internal_samples = static_cast<uint8_t *>(heap_caps_aligned_alloc(
      internal_alignment, kSampleCount,
      MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  psram_samples = static_cast<uint8_t *>(heap_caps_aligned_alloc(
      external_alignment, kSampleCount, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  print_buffer("internal", internal_samples, internal_alignment);
  print_buffer("psram", psram_samples, external_alignment);
  if (internal_samples == nullptr || psram_samples == nullptr) {
    Serial.println("DONE status=alloc-failed");
    cleanup();
    return;
  }

  const esp_err_t pwm_result = configure_pwm();
  const esp_err_t parlio_result =
      pwm_result == ESP_OK ? configure_parlio() : ESP_FAIL;
  Serial.print("CONFIG pwm=");
  Serial.print(esp_err_to_name(pwm_result));
  Serial.print(" parlio=");
  Serial.println(esp_err_to_name(parlio_result));
  if (pwm_result != ESP_OK || parlio_result != ESP_OK) {
    Serial.println("DONE status=config-failed");
    cleanup();
    return;
  }

  for (size_t run = 0; run < kCaptureRuns; ++run) {
    run_capture("internal", internal_samples, run);
  }
  for (size_t run = 0; run < kCaptureRuns; ++run) {
    run_capture("psram", psram_samples, run);
  }
  cleanup();
  Serial.println("DONE status=ok");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  if (!parse_pins()) {
    Serial.println("E016 invalid PARLIO_PINS");
  }
}

void loop() {
  if (Serial.available() > 0 && Serial.read() == '?') {
    run_experiment();
  }
  delay(1);
}
