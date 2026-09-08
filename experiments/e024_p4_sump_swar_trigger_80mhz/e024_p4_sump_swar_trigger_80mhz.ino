// E024 searches four 8-bit samples at a time during the E022 capture path.
#include <Arduino.h>
#include <esp_timer.h>

#ifndef EXPERIMENT_ID
#define EXPERIMENT_ID "E024"
#endif

#ifndef SAMPLE_RATE_HZ
#define SAMPLE_RATE_HZ 80000000
#endif

#ifndef TRIGGER_MODE_FOR_RUN
#define TRIGGER_MODE_FOR_RUN(run) (run)
#endif

#ifndef TRIGGER_MIN_INDEX_FOR_RUN
#define TRIGGER_MIN_INDEX_FOR_RUN(run) 0
#endif

namespace trigger_test {

struct Metrics {
  size_t run;
  size_t mode;
  bool found;
  size_t first_index;
  size_t scanned;
  int64_t scan_us;
  uint8_t previous;
  bool has_previous;
};

Metrics metrics = {};
const char *const mode_names[] = {"nomatch", "rising", "pattern"};

void before(size_t run) {
  metrics = {};
  metrics.run = run;
  metrics.mode = TRIGGER_MODE_FOR_RUN(run);
}

inline __attribute__((always_inline)) uint32_t load_word(const uint8_t *data) {
  uint32_t word;
  memcpy(&word, data, sizeof(word));
  return word;
}

inline __attribute__((always_inline)) bool has_zero_byte(uint32_t value) {
  return ((value - 0x01010101U) & ~value & 0x80808080U) != 0;
}

void record_pattern_word(const uint8_t *data, size_t offset, uint8_t mask,
                         uint8_t value) {
  if (metrics.found) return;
  for (size_t i = 0; i < sizeof(uint32_t); ++i) {
    if ((data[i] & mask) == value) {
      metrics.found = true;
      metrics.first_index = offset + i;
      return;
    }
  }
}

void scan_pattern(const uint8_t *data, size_t length, size_t offset,
                  uint8_t mask, uint8_t value) {
  const uint32_t masks = 0x01010101U * mask;
  const uint32_t values = 0x01010101U * value;
  size_t i = 0;
  for (; i + sizeof(uint32_t) <= length; i += sizeof(uint32_t)) {
    const uint32_t difference = (load_word(data + i) & masks) ^ values;
    if (!metrics.found && has_zero_byte(difference)) {
      record_pattern_word(data + i, offset + i, mask, value);
    }
  }
  for (; i < length; ++i) {
    if (!metrics.found && (data[i] & mask) == value) {
      metrics.found = true;
      metrics.first_index = offset + i;
    }
  }
}

void record_rising_word(const uint8_t *data, size_t offset) {
  if (metrics.found) return;
  uint8_t previous = metrics.previous;
  bool has_previous = metrics.has_previous;
  for (size_t i = 0; i < sizeof(uint32_t); ++i) {
    if (offset + i >= TRIGGER_MIN_INDEX_FOR_RUN(metrics.run) && has_previous &&
        !(previous & 0x01U) && (data[i] & 0x01U)) {
      metrics.found = true;
      metrics.first_index = offset + i;
      return;
    }
    previous = data[i];
    has_previous = true;
  }
}

void scan_rising(const uint8_t *data, size_t length, size_t offset) {
  size_t i = 0;
  for (; i + sizeof(uint32_t) <= length; i += sizeof(uint32_t)) {
    const uint32_t word = load_word(data + i);
    const uint32_t current = word & 0x01010101U;
    const uint32_t previous =
        ((current << 8) & 0x01010100U) |
        (metrics.has_previous ? (metrics.previous & 0x01U) : current & 0x01U);
    if (!metrics.found && (current & ~previous) != 0) {
      record_rising_word(data + i, offset + i);
    }
    metrics.previous = data[i + sizeof(uint32_t) - 1];
    metrics.has_previous = true;
  }
  for (; i < length; ++i) {
    const uint8_t sample = data[i];
    if (!metrics.found && offset + i >= TRIGGER_MIN_INDEX_FOR_RUN(metrics.run) &&
        metrics.has_previous &&
        !(metrics.previous & 0x01U) && (sample & 0x01U)) {
      metrics.found = true;
      metrics.first_index = offset + i;
    }
    metrics.previous = sample;
    metrics.has_previous = true;
  }
}

void scan(const uint8_t *data, size_t length, size_t offset) {
  const int64_t begin = esp_timer_get_time();
  if (metrics.mode == 0) {
    scan_pattern(data, length, offset, 0xFFU, 0x55U);
  } else if (metrics.mode == 1) {
    scan_rising(data, length, offset);
  } else {
    scan_pattern(data, length, offset, 0x0FU, 0x00U);
  }
  metrics.scanned += length;
  metrics.scan_us += esp_timer_get_time() - begin;
}

void after(size_t run) {
  const uint64_t scan_mbps_milli =
      metrics.scan_us > 0 ? metrics.scanned * 1000ULL / metrics.scan_us : 0;
  Serial.print("TRIGGER run=");
  Serial.print(run);
  Serial.print(" mode=");
  Serial.print(mode_names[metrics.mode]);
  Serial.print(" found=");
  Serial.print(metrics.found);
  Serial.print(" index=");
  Serial.print(metrics.first_index);
  Serial.print(" scanned=");
  Serial.print(metrics.scanned);
  Serial.print(" scan_us=");
  Serial.print(metrics.scan_us);
  Serial.print(" scan_mbps_milli=");
  Serial.println(scan_mbps_milli);
}

}  // namespace trigger_test

#ifndef BEFORE_CAPTURE
#define BEFORE_CAPTURE(run) trigger_test::before(run)
#endif
#ifndef PROCESS_CHUNK
#define PROCESS_CHUNK(data, length, offset) \
  trigger_test::scan(data, length, offset)
#endif
#ifndef AFTER_CAPTURE
#define AFTER_CAPTURE(run) trigger_test::after(run)
#endif
#include "../e021_p4_parlio_psram_spool/e021_p4_parlio_psram_spool.ino"
