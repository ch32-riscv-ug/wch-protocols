// Timing helper kept out of the .ino: the Arduino preprocessor cannot emit a
// prototype for a template function and breaks the build.
#pragma once
#include <Arduino.h>
#include <esp_timer.h>

struct Timing {
  uint32_t count;
  int64_t total_us;
};

#define E151_BARRIER() asm volatile("" ::: "memory")

template <typename F>
static inline Timing e151_timed(uint32_t n, bool quiet, F body) {
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  if (quiet) portENTER_CRITICAL(&mux);
  const int64_t t0 = esp_timer_get_time();
  for (uint32_t i = 0; i < n; ++i) { body(i); E151_BARRIER(); }
  const int64_t t1 = esp_timer_get_time();
  if (quiet) portEXIT_CRITICAL(&mux);
  return {n, t1 - t0};
}
