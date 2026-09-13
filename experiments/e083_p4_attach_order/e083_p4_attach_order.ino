// E083: does the order of PARLIO's GPIO-matrix input and LEDC's output attach
// explain the channel that intermittently reads as a flat zero?
// Plan and report: README.ja.md
//
// E075 saw it three times, always on the first capture of a sweep, always a
// different lane, never reproducible by re-running the same condition, and
// always with overflow=0 -- so not a drop in the capture path. Its firmware
// created the PARLIO receiver before attaching LEDC, and ledcAttach touching a
// pin the matrix had already claimed was the suspect. Here the order is a
// parameter, each trial resets the pins first so every one starts from the same
// place, and the lanes are checked on the board so hundreds of trials are cheap.

#include <Arduino.h>
#include <HWCDC.h>
#include <driver/gpio.h>
#include <driver/parlio_rx.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#ifndef BANNER_GIT
#define BANNER_GIT "unknown"
#endif
#ifndef PARLIO_PINS
#define PARLIO_PINS "2,3,4,5,6,7,8,9"
#endif

// The one capture that E075 ever saw fail was the first one after a boot, so
// the firmware takes one by itself before any command arrives and reports the
// lanes. BOOT_ORDER picks which order that first capture uses; the host power
// cycles the board to collect more of them.
#ifndef BOOT_ORDER
#define BOOT_ORDER 0
#endif
#ifndef BOOT_RATE_HZ
#define BOOT_RATE_HZ 160000000
#endif
#ifndef LANE_COUNT
#define LANE_COUNT 2
#endif
static constexpr size_t kLaneCount = LANE_COUNT;  // 8 / kLaneCount samples per byte
static constexpr size_t kParsePins = 8;
static constexpr uint32_t kPwmFrequencyHz = 100000;
static constexpr uint8_t kPwmResolutionBits = 8;
// Distinct duties per lane so the host can tell the channels apart.
static constexpr uint32_t kAllDuties[8] = {32, 64, 96, 128, 160, 176, 192, 208};
static constexpr size_t kRingSize = 64 * 1024;
static constexpr size_t kDelimiterSize = 65408;
static constexpr size_t kQueueDepth = 64;
static constexpr size_t kCaptureBytesMax = 4u * 1024u * 1024u;

static HWCDC Console;

struct Chunk {
  const uint8_t *data;
  size_t length;
};

struct CaptureState {
  QueueHandle_t queue;
  volatile size_t callback_bytes;
  volatile size_t queue_overflow;
};

static int pins[kParsePins];
static bool ledc_attached[kLaneCount];
static uint8_t *ring_buffer;
static uint8_t *capture;
static CaptureState capture_state;

static size_t capture_bytes;
static esp_err_t capture_status;
static bool capture_timed_out;
static uint32_t capture_rate_hz;
static uint64_t capture_elapsed_us;
static bool host_armed;
static char boot_line[192];
static uint32_t boot_line_at;
static bool boot_acknowledged;
static char command[64];
static size_t command_length;

static bool parse_pins(void) {
  const char *cursor = PARLIO_PINS;
  for (size_t lane = 0; lane < kParsePins; ++lane) {
    char *end = nullptr;
    const long value = strtol(cursor, &end, 10);
    if (end == cursor || value < 0 || value >= GPIO_NUM_MAX) {
      return false;
    }
    pins[lane] = static_cast<int>(value);
    if (lane + 1 < kParsePins) {
      if (*end != ',') {
        return false;
      }
      cursor = end + 1;
    }
  }
  return true;
}

static esp_err_t configure_pwm(void) {
  for (size_t lane = 0; lane < kLaneCount; ++lane) {
    if (!ledcAttachChannel(pins[lane], kPwmFrequencyHz, kPwmResolutionBits, lane)) {
      return ESP_FAIL;
    }
    ledc_attached[lane] = true;
    if (!ledcWrite(pins[lane], kAllDuties[lane])) {
      return ESP_FAIL;
    }
  }
  return ESP_OK;
}

static void cleanup_pwm(void) {
  for (size_t lane = 0; lane < kLaneCount; ++lane) {
    if (ledc_attached[lane]) {
      ledcDetach(pins[lane]);
      ledc_attached[lane] = false;
    }
    gpio_reset_pin(static_cast<gpio_num_t>(pins[lane]));
  }
}

static bool IRAM_ATTR on_partial_receive(
  parlio_rx_unit_handle_t, const parlio_rx_event_data_t *event, void *user_data
) {
  auto *state = static_cast<CaptureState *>(user_data);
  const Chunk chunk = {static_cast<const uint8_t *>(event->data), event->recv_bytes};
  state->callback_bytes += event->recv_bytes;
  BaseType_t high_task_woken = pdFALSE;
  if (xQueueSendFromISR(state->queue, &chunk, &high_task_woken) != pdTRUE) {
    ++state->queue_overflow;
  }
  return high_task_woken == pdTRUE;
}

// Rising edges per lane over the captured buffer. A lane that never rises is
// the symptom E075 recorded; counting here keeps a trial to a few milliseconds
// instead of a console download.
static void analyse(size_t bytes, uint32_t *edges, uint32_t *high) {
  const size_t per_byte = 8 / kLaneCount;
  const uint8_t mask = static_cast<uint8_t>((1u << kLaneCount) - 1u);
  uint8_t previous[8] = {0};
  for (size_t lane = 0; lane < kLaneCount; ++lane) {
    edges[lane] = 0;
    high[lane] = 0;
  }
  for (size_t index = 0; index < bytes; ++index) {
    const uint8_t value = capture[index];
    for (size_t slot = 0; slot < per_byte; ++slot) {
      const uint8_t sample = static_cast<uint8_t>((value >> (kLaneCount * slot)) & mask);
      for (size_t lane = 0; lane < kLaneCount; ++lane) {
        const uint8_t bit = static_cast<uint8_t>((sample >> lane) & 1u);
        if (bit) {
          ++high[lane];
          if (!previous[lane]) {
            ++edges[lane];
          }
        }
        previous[lane] = bit;
      }
    }
  }
}

static esp_err_t create_receiver(
  parlio_rx_unit_handle_t *rx_unit, parlio_rx_delimiter_handle_t *delimiter, uint32_t sample_rate_hz
) {
  parlio_rx_unit_config_t unit_config = {};
  unit_config.trans_queue_depth = 1;
  unit_config.max_recv_size = kRingSize;
  unit_config.dma_burst_size = 0;
  unit_config.data_width = kLaneCount;
  unit_config.clk_src = PARLIO_CLK_SRC_DEFAULT;
  unit_config.exp_clk_freq_hz = sample_rate_hz;
  unit_config.clk_in_gpio_num = GPIO_NUM_NC;
  unit_config.clk_out_gpio_num = GPIO_NUM_NC;
  unit_config.valid_gpio_num = GPIO_NUM_NC;
  for (size_t lane = 0; lane < PARLIO_RX_UNIT_MAX_DATA_WIDTH; ++lane) {
    unit_config.data_gpio_nums[lane] = lane < kLaneCount ? static_cast<gpio_num_t>(pins[lane]) : GPIO_NUM_NC;
  }
  unit_config.flags.io_loop_back = false;
  esp_err_t result = parlio_new_rx_unit(&unit_config, rx_unit);
  if (result != ESP_OK) {
    return result;
  }
  parlio_rx_event_callbacks_t callbacks = {};
  callbacks.on_partial_receive = on_partial_receive;
  result = parlio_rx_unit_register_event_callbacks(*rx_unit, &callbacks, &capture_state);
  if (result != ESP_OK) {
    return result;
  }
  parlio_rx_soft_delimiter_config_t delimiter_config = {};
  delimiter_config.sample_edge = PARLIO_SAMPLE_EDGE_POS;
  delimiter_config.bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB;
  delimiter_config.eof_data_len = kDelimiterSize;
  return parlio_new_rx_soft_delimiter(&delimiter_config, delimiter);
}

// order 0 = receiver first, then LEDC (what E075 did). order 1 = LEDC first.
static void run_capture(size_t want_bytes, uint32_t rate_hz, int order) {
  capture_bytes = 0;
  capture_rate_hz = rate_hz;
  capture_state.callback_bytes = 0;
  capture_state.queue_overflow = 0;
  xQueueReset(capture_state.queue);
  memset(ring_buffer, 0xA5, kRingSize);
  esp_cache_msync(ring_buffer, kRingSize, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

  // Start every trial from the same pin state, so trial N looks like a fresh
  // boot rather than like whatever the previous trial left behind.
  for (size_t lane = 0; lane < kLaneCount; ++lane) {
    gpio_reset_pin(static_cast<gpio_num_t>(pins[lane]));
  }

  parlio_rx_unit_handle_t rx_unit = nullptr;
  parlio_rx_delimiter_handle_t delimiter = nullptr;
  esp_err_t status = ESP_OK;
  if (order == 0) {
    status = create_receiver(&rx_unit, &delimiter, rate_hz);
    if (status == ESP_OK) {
      status = configure_pwm();
    }
  } else {
    status = configure_pwm();
    if (status == ESP_OK) {
      status = create_receiver(&rx_unit, &delimiter, rate_hz);
    }
  }
  if (status == ESP_OK) {
    status = parlio_rx_unit_enable(rx_unit, true);
  }
  if (status == ESP_OK) {
    parlio_receive_config_t receive_config = {};
    receive_config.delimiter = delimiter;
    receive_config.flags.partial_rx_en = true;
    receive_config.flags.indirect_mount = false;
    status = parlio_rx_unit_receive(rx_unit, ring_buffer, kRingSize, &receive_config);
  }

  size_t copied = 0;
  bool timed_out = false;
  const uint64_t started = esp_timer_get_time();
  if (status == ESP_OK) {
    status = parlio_rx_soft_delimiter_start_stop(rx_unit, delimiter, true);
  }
  while (status == ESP_OK && copied < want_bytes) {
    Chunk chunk = {};
    if (xQueueReceive(capture_state.queue, &chunk, pdMS_TO_TICKS(500)) != pdTRUE) {
      timed_out = true;
      break;
    }
    const size_t step = (want_bytes - copied) < chunk.length ? (want_bytes - copied) : chunk.length;
    memcpy(capture + copied, chunk.data, step);
    copied += step;
  }
  capture_elapsed_us = esp_timer_get_time() - started;
  capture_bytes = copied;

  if (rx_unit) {
    parlio_rx_soft_delimiter_start_stop(rx_unit, delimiter, false);
    parlio_rx_unit_disable(rx_unit);
  }
  if (delimiter) {
    parlio_del_rx_delimiter(delimiter);
  }
  if (rx_unit) {
    parlio_del_rx_unit(rx_unit);
  }
  cleanup_pwm();

  capture_status = status;
  capture_timed_out = timed_out;
}

// Raw bytes follow the DUMP line with nothing else interleaved, so the host can
// read exactly the count it was told.
static void run_dump(void) {
  Console.printf("DUMP bytes=%lu\n", static_cast<unsigned long>(capture_bytes));
  Console.flush();
  size_t sent = 0;
  while (sent < capture_bytes) {
    const size_t step = (capture_bytes - sent) < 4096 ? (capture_bytes - sent) : 4096;
    const size_t written = Console.write(capture + sent, step);
    if (written == 0) {
      delay(1);
      continue;
    }
    sent += written;
  }
  Console.flush();
}

static void handle_command(void) {
  if (command[0] == '?') {
    Console.printf("# EXP E083 v1 git=%s probe=esp32p4_parlio target=internal build=%s %s\n", BANNER_GIT, __DATE__,
                   __TIME__);
    Console.printf(
      "ENV chip=%s psram_found=%u psram_size=%lu lanes=%u pins=%d,%d pwm_hz=%lu duties=%lu,%lu capture_max=%lu "
      "ring=%lu samples_per_byte=%u ready=%u\n",
      ESP.getChipModel(), psramFound() ? 1U : 0U, static_cast<unsigned long>(ESP.getPsramSize()), kLaneCount, pins[0],
      pins[1], static_cast<unsigned long>(kPwmFrequencyHz), static_cast<unsigned long>(kAllDuties[0]),
      static_cast<unsigned long>(kAllDuties[1]), static_cast<unsigned long>(kCaptureBytesMax),
      static_cast<unsigned long>(kRingSize), (unsigned)(8 / kLaneCount), (capture && ring_buffer) ? 1U : 0U
    );
    Console.flush();
    return;
  }
  if (command[0] == 'C') {
    unsigned long bytes = 0;
    unsigned long rate = 0;
    unsigned long order = 0;
    if (sscanf(command + 1, "%lu %lu %lu", &bytes, &rate, &order) < 2 || bytes == 0 || bytes > kCaptureBytesMax ||
        rate == 0) {
      Console.printf("CAP status=reject bytes=%lu rate_hz=%lu\n", bytes, rate);
      Console.flush();
      return;
    }
    run_capture(bytes, static_cast<uint32_t>(rate), static_cast<int>(order));
    Console.printf("CAP status=%d lanes=%u rate_hz=%lu order=%lu bytes=%lu overflow=%lu timeout=%u\n",
                   static_cast<int>(capture_status), (unsigned)kLaneCount, rate, order,
                   static_cast<unsigned long>(capture_bytes),
                   static_cast<unsigned long>(capture_state.queue_overflow), capture_timed_out ? 1U : 0U);
    Console.flush();
    return;
  }
  // O <order> <trials> <rate> [bytes] -- repeat a trial and check the lanes on
  // the board. One line per trial; the host only has to count the dead ones.
  if (command[0] == 'O') {
    unsigned long order = 0;
    unsigned long trials = 0;
    unsigned long rate = 0;
    unsigned long bytes = 65536;
    const int parsed = sscanf(command + 1, "%lu %lu %lu %lu", &order, &trials, &rate, &bytes);
    if (parsed < 3 || trials == 0 || rate == 0 || bytes == 0 || bytes > kCaptureBytesMax) {
      Console.println("ORDER status=reject");
      Console.flush();
      return;
    }
    Console.printf("ORDER begin order=%lu trials=%lu rate_hz=%lu bytes=%lu lanes=%u\n", order, trials, rate, bytes,
                   (unsigned)kLaneCount);
    Console.flush();
    unsigned long dead_trials = 0;
    for (unsigned long trial = 0; trial < trials; ++trial) {
      run_capture(bytes, static_cast<uint32_t>(rate), static_cast<int>(order));
      uint32_t edges[8] = {0};
      uint32_t high[8] = {0};
      analyse(capture_bytes, edges, high);
      uint32_t dead = 0;
      for (size_t lane = 0; lane < kLaneCount; ++lane) {
        if (edges[lane] == 0) {
          dead |= (1u << lane);
        }
      }
      if (dead) {
        ++dead_trials;
      }
      Console.printf("T trial=%lu order=%lu status=%d bytes=%lu overflow=%lu dead=0x%02lx edges=", trial,
                     order, static_cast<int>(capture_status), static_cast<unsigned long>(capture_bytes),
                     static_cast<unsigned long>(capture_state.queue_overflow), static_cast<unsigned long>(dead));
      for (size_t lane = 0; lane < kLaneCount; ++lane) {
        Console.printf("%lu%s", static_cast<unsigned long>(edges[lane]), lane + 1 < kLaneCount ? "," : "");
      }
      Console.printf(" high=");
      for (size_t lane = 0; lane < kLaneCount; ++lane) {
        Console.printf("%lu%s", static_cast<unsigned long>(high[lane]), lane + 1 < kLaneCount ? "," : "");
      }
      Console.println();
      Console.flush();
    }
    Console.printf("ORDER done order=%lu trials=%lu dead_trials=%lu\n", order, trials, dead_trials);
    Console.flush();
    return;
  }
  if (command[0] == 'D') {
    run_dump();
    return;
  }
  if (command[0] == 'R') {
    Console.println("RESTART");
    Console.flush();
    delay(50);
    esp_restart();
    return;
  }
  Console.println("CFG status=unknown");
  Console.flush();
}

void setup() {
  Console.begin();
  parse_pins();
  capture_state.queue = xQueueCreate(kQueueDepth, sizeof(Chunk));
  ring_buffer = static_cast<uint8_t *>(heap_caps_aligned_alloc(64, kRingSize, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  capture = static_cast<uint8_t *>(heap_caps_malloc(kCaptureBytesMax, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

void loop() {
  if (!host_armed) {
    if (millis() < 1000) {
      delay(10);
      return;
    }
    while (Console.available()) {
      Console.read();
    }
    // First capture of this boot, before anything else has touched the pins.
    run_capture(65536, BOOT_RATE_HZ, BOOT_ORDER);
    uint32_t edges[8] = {0};
    uint32_t high[8] = {0};
    analyse(capture_bytes, edges, high);
    uint32_t dead = 0;
    for (size_t lane = 0; lane < kLaneCount; ++lane) {
      if (edges[lane] == 0) {
        dead |= (1u << lane);
      }
    }
    // Held and repeated rather than printed once: after a restart the host has
    // to wait for the port to come back, and by then a single line is gone.
    int written = snprintf(boot_line, sizeof(boot_line),
                           "BOOT order=%d rate_hz=%lu lanes=%u status=%d bytes=%lu overflow=%lu dead=0x%02lx edges=",
                           BOOT_ORDER, (unsigned long)BOOT_RATE_HZ, (unsigned)kLaneCount,
                           static_cast<int>(capture_status), static_cast<unsigned long>(capture_bytes),
                           static_cast<unsigned long>(capture_state.queue_overflow),
                           static_cast<unsigned long>(dead));
    for (size_t lane = 0; lane < kLaneCount && written > 0 && written < (int)sizeof(boot_line); ++lane) {
      written += snprintf(boot_line + written, sizeof(boot_line) - written, "%lu%s",
                          static_cast<unsigned long>(edges[lane]), lane + 1 < kLaneCount ? "," : "");
    }
    host_armed = true;
    return;
  }
  if (!Console.available()) {
    // Repeat the boot result until someone is listening.
    if (!boot_acknowledged && millis() - boot_line_at >= 500) {
      boot_line_at = millis();
      Console.println(boot_line);
      Console.println("READY E083");
      Console.flush();
    }
    delay(1);
    return;
  }
  boot_acknowledged = true;
  while (Console.available()) {
    const int value = Console.read();
    if (value < 0) {
      break;
    }
    const char received = static_cast<char>(value);
    if (received == '\r') {
      continue;
    }
    if (received != '\n') {
      if (command_length < sizeof(command) - 1) {
        command[command_length++] = received;
      }
      continue;
    }
    command[command_length] = '\0';
    if (command_length > 0) {
      handle_command();
    }
    command_length = 0;
  }
}
