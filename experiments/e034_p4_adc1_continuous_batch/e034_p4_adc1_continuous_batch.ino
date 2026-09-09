#include <Arduino.h>
#include <esp_adc/adc_continuous.h>
#include <esp_heap_caps.h>
#include <soc/soc_caps.h>

#ifndef BANNER_GIT
#define BANNER_GIT "unknown"
#endif

static constexpr uint32_t kTargetConversions = 262144;
static constexpr uint32_t kResultBytes = SOC_ADC_DIGI_RESULT_BYTES;
static constexpr uint32_t kTargetBytes = kTargetConversions * kResultBytes;
static constexpr uint32_t kFrameBytes = 1024;
static constexpr uint32_t kPoolBytes = 16384;
static uint8_t read_buffer[kFrameBytes];
static volatile uint32_t pool_overflows;
static bool host_armed;

static bool IRAM_ATTR on_pool_overflow(adc_continuous_handle_t,
                                       const adc_continuous_evt_data_t *,
                                       void *) {
  ++pool_overflows;
  return false;
}

static void run_case(uint8_t channel_count, uint32_t requested_rate) {
  adc_continuous_handle_t handle = nullptr;
  adc_continuous_handle_cfg_t handle_config = {};
  handle_config.max_store_buf_size = kPoolBytes;
  handle_config.conv_frame_size = kFrameBytes;

  esp_err_t create_result = adc_continuous_new_handle(&handle_config, &handle);
  esp_err_t config_result = ESP_FAIL;
  esp_err_t callbacks_result = ESP_FAIL;
  esp_err_t start_result = ESP_FAIL;
  esp_err_t stop_result = ESP_FAIL;
  esp_err_t delete_result = ESP_FAIL;
  esp_err_t last_read_result = ESP_FAIL;
  uint8_t *destination = static_cast<uint8_t *>(
      heap_caps_malloc(kTargetBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  uint32_t copied = 0;
  uint32_t reads = 0;
  uint32_t timeouts = 0;
  uint64_t started_us = 0;
  uint64_t stopped_us = 0;
  pool_overflows = 0;

  adc_digi_pattern_config_t patterns[8] = {};
  for (uint8_t i = 0; i < channel_count; ++i) {
    patterns[i].atten = ADC_ATTEN_DB_12;
    patterns[i].channel = i;
    patterns[i].unit = ADC_UNIT_1;
    patterns[i].bit_width = ADC_BITWIDTH_12;
  }

  if (create_result == ESP_OK) {
    adc_continuous_config_t adc_config = {};
    adc_config.pattern_num = channel_count;
    adc_config.adc_pattern = patterns;
    adc_config.sample_freq_hz = requested_rate;
    adc_config.conv_mode = ADC_CONV_SINGLE_UNIT_1;
    adc_config.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
    config_result = adc_continuous_config(handle, &adc_config);
  }
  if (config_result == ESP_OK) {
    adc_continuous_evt_cbs_t callbacks = {};
    callbacks.on_pool_ovf = on_pool_overflow;
    callbacks_result = adc_continuous_register_event_callbacks(handle, &callbacks, nullptr);
  }
  if (callbacks_result == ESP_OK && destination != nullptr) {
    start_result = adc_continuous_start(handle);
  }
  if (start_result == ESP_OK) {
    started_us = esp_timer_get_time();
    while (copied < kTargetBytes) {
      uint32_t bytes_read = 0;
      last_read_result = adc_continuous_read(handle, read_buffer,
                                              sizeof(read_buffer), &bytes_read,
                                              1000);
      if (last_read_result == ESP_ERR_TIMEOUT) {
        ++timeouts;
        if (timeouts >= 3) break;
        continue;
      }
      if (last_read_result != ESP_OK || bytes_read == 0) break;
      uint32_t remaining = kTargetBytes - copied;
      uint32_t to_copy = min(bytes_read, remaining);
      memcpy(destination + copied, read_buffer, to_copy);
      copied += to_copy;
      ++reads;
    }
    stopped_us = esp_timer_get_time();
    stop_result = adc_continuous_stop(handle);
  }
  if (handle != nullptr) delete_result = adc_continuous_deinit(handle);

  uint32_t invalid_unit = 0;
  uint32_t invalid_channel = 0;
  uint32_t order_errors = 0;
  uint8_t pattern_phase = 0;
  uint32_t channel_samples[8] = {};
  uint16_t channel_min[8];
  uint16_t channel_max[8] = {};
  for (uint8_t i = 0; i < 8; ++i) channel_min[i] = UINT16_MAX;

  if (destination != nullptr) {
    uint32_t conversions = copied / kResultBytes;
    auto *results = reinterpret_cast<adc_digi_output_data_t *>(destination);
    if (conversions > 0) pattern_phase = results[0].type2.channel;
    for (uint32_t i = 0; i < conversions; ++i) {
      uint8_t unit = results[i].type2.unit;
      uint8_t channel = results[i].type2.channel;
      uint16_t value = results[i].type2.data;
      if (unit != 0) ++invalid_unit;
      if (channel >= channel_count) {
        ++invalid_channel;
        continue;
      }
      if (channel != (pattern_phase + i) % channel_count) ++order_errors;
      ++channel_samples[channel];
      channel_min[channel] = min(channel_min[channel], value);
      channel_max[channel] = max(channel_max[channel], value);
    }
  }

  uint64_t elapsed_us = stopped_us > started_us ? stopped_us - started_us : 0;
  uint32_t effective_rate = elapsed_us ?
      static_cast<uint32_t>((static_cast<uint64_t>(copied / kResultBytes) * 1000000ULL) / elapsed_us) : 0;
  bool ok = destination != nullptr && create_result == ESP_OK &&
            config_result == ESP_OK && callbacks_result == ESP_OK &&
            start_result == ESP_OK && last_read_result == ESP_OK &&
            stop_result == ESP_OK && delete_result == ESP_OK &&
            copied == kTargetBytes && pool_overflows == 0 &&
            invalid_unit == 0 && invalid_channel == 0;

  Serial.printf("CASE channels=%u rate_hz=%lu result=%s create=%s config=%s callbacks=%s start=%s read=%s stop=%s deinit=%s psram=%u target=%lu copied=%lu reads=%lu timeouts=%lu overflows=%lu elapsed_us=%llu effective_rate_hz=%lu invalid_unit=%lu invalid_channel=%lu pattern_phase=%u order_errors=%lu",
                channel_count, static_cast<unsigned long>(requested_rate),
                ok ? "ESP_OK" : "ESP_FAIL", esp_err_to_name(create_result),
                esp_err_to_name(config_result), esp_err_to_name(callbacks_result),
                esp_err_to_name(start_result), esp_err_to_name(last_read_result),
                esp_err_to_name(stop_result), esp_err_to_name(delete_result),
                destination != nullptr, static_cast<unsigned long>(kTargetBytes),
                static_cast<unsigned long>(copied), static_cast<unsigned long>(reads),
                static_cast<unsigned long>(timeouts), static_cast<unsigned long>(pool_overflows),
                static_cast<unsigned long long>(elapsed_us),
                static_cast<unsigned long>(effective_rate),
                static_cast<unsigned long>(invalid_unit),
                static_cast<unsigned long>(invalid_channel), pattern_phase,
                static_cast<unsigned long>(order_errors));
  for (uint8_t i = 0; i < channel_count; ++i) {
    Serial.printf(" c%u=%lu:%u:%u", i,
                  static_cast<unsigned long>(channel_samples[i]),
                  channel_min[i], channel_max[i]);
  }
  Serial.println();
  Serial.flush();
  if (destination != nullptr) heap_caps_free(destination);
  delay(150);
}

void setup() {
  Serial.begin(115200);
}

void loop() {
  if (!host_armed) {
    if (millis() < 1000) {
      delay(10);
      return;
    }
    while (Serial.available()) Serial.read();
    Serial.println("READY E034");
    Serial.flush();
    host_armed = true;
    return;
  }
  if (!Serial.available()) {
    delay(10);
    return;
  }
  while (Serial.available()) Serial.read();
  Serial.printf("# EXP E034 v1 git=%s probe=esp32p4_adc target=floating build=%s %s\n",
                BANNER_GIT, __DATE__, __TIME__);
  Serial.printf("ENV psram_found=%u psram_size=%lu adc_units=%u adc1_channels=%u result_bytes=%u rate_min=%u rate_max=%u target_conversions=%lu\n",
                psramFound(), static_cast<unsigned long>(ESP.getPsramSize()),
                SOC_ADC_PERIPH_NUM, SOC_ADC_CHANNEL_NUM(0),
                SOC_ADC_DIGI_RESULT_BYTES, SOC_ADC_SAMPLE_FREQ_THRES_LOW,
                SOC_ADC_SAMPLE_FREQ_THRES_HIGH,
                static_cast<unsigned long>(kTargetConversions));
  const uint32_t rates[] = {10000, 40000, 83333};
  const uint8_t widths[] = {1, 2, 4, 8};
  Serial.flush();
  for (uint8_t width : widths) {
    for (uint32_t rate : rates) run_case(width, rate);
  }
  Serial.println("DONE status=ok");
  Serial.flush();
}
