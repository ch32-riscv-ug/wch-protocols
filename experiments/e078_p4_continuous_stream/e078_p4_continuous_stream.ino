// E078: stream a PARLIO capture straight out of the OTG HS vendor endpoint
// instead of filling PSRAM first, and find the rate where it stops keeping up.
//
// Built against the EspUsbDevice working tree (CR-4/CR-7/CR-9), not a release:
// the TX FIFO and the per-transfer size are set in build_opt.h, and the sender
// blocks on waitWritable() rather than spinning.
// Plan and report: README.ja.md
//
// E076 captured, then sent. Here the two run together with an elastic FIFO in
// PSRAM between them: PARLIO fills the internal DMA ring, a harvest task moves
// chunks into the FIFO, and the USB task drains it. The FIFO exists to absorb
// the moments when the host is not reading, so it running dry is fine and it
// filling up is the failure being measured.

#include <Arduino.h>
#include <HWCDC.h>

#include "EspUsbDevice.h"
#include <driver/gpio.h>
#include <driver/parlio_rx.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#ifndef BANNER_GIT
#define BANNER_GIT "unknown"
#endif
#ifndef PARLIO_PINS
#define PARLIO_PINS "2,3,4,5,6,7,8,9"
#endif

static constexpr size_t kLaneCount = 2;
static constexpr size_t kParsePins = 8;
static constexpr uint32_t kPwmFrequencyHz = 100000;
static constexpr uint8_t kPwmResolutionBits = 8;
static constexpr uint32_t kDuties[kLaneCount] = {64, 128};
static constexpr size_t kRingSize = 64 * 1024;
static constexpr size_t kDelimiterSize = 65408;
static constexpr size_t kQueueDepth = 128;
static constexpr size_t kFifoSize = 8u * 1024u * 1024u;
static constexpr size_t kUsbChunk = 16 * 1024;
// What one waitWritable() asks for. It is clamped to writeCapacity() (= the TX
// FIFO, 4 KiB here), so a slice keeps the sender moving as soon as any room
// appears rather than waiting for the FIFO to drain completely.
static constexpr size_t kWaitSlice = 1024;
static constexpr uint32_t kWaitTimeoutMs = 1000;
static constexpr size_t kStreamBytesMax = 64u * 1024u * 1024u;

static constexpr uint16_t kTestVid = 0x1209;
static constexpr uint16_t kTestPid = 0x0008;
static constexpr BaseType_t kUsbCore = 0;      // E067: USB on core 0,
static constexpr BaseType_t kHarvestCore = 1;  // harvest on core 1

static HWCDC Console;
static EspUsbDevice device;
static EspUsbDeviceVendor HsVendor(device, 512);
static bool usb_ready;

struct Chunk {
  const uint8_t *data;
  size_t length;
};

// Single producer (harvest), single consumer (usb). head and tail each have one
// writer, so the counts need no lock -- only that each is written after the
// bytes it describes are in place.
struct Fifo {
  uint8_t *buffer;
  volatile size_t head;  // bytes written by the producer
  volatile size_t tail;  // bytes read by the consumer
  volatile size_t high_water;
  volatile uint32_t overflow;
};

static Fifo fifo;
static QueueHandle_t chunk_queue;
static volatile size_t ring_overflow;
static volatile size_t callback_bytes;

static int pins[kParsePins];
static bool ledc_attached[kLaneCount];
static uint8_t *ring_buffer;

static volatile bool stream_active;
static volatile size_t stream_target;
static volatile size_t harvested;
static volatile size_t usb_sent;
static volatile uint32_t usb_stalls;
static volatile uint32_t usb_waits;
static volatile uint32_t usb_timeouts;
static volatile uint64_t usb_elapsed_us;
static SemaphoreHandle_t usb_done;
static SemaphoreHandle_t harvest_done;

static bool host_armed;
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
    if (!ledcWrite(pins[lane], kDuties[lane])) {
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
  parlio_rx_unit_handle_t, const parlio_rx_event_data_t *event, void *
) {
  const Chunk chunk = {static_cast<const uint8_t *>(event->data), event->recv_bytes};
  callback_bytes += event->recv_bytes;
  BaseType_t high_task_woken = pdFALSE;
  if (xQueueSendFromISR(chunk_queue, &chunk, &high_task_woken) != pdTRUE) {
    ++ring_overflow;
  }
  return high_task_woken == pdTRUE;
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
  result = parlio_rx_unit_register_event_callbacks(*rx_unit, &callbacks, nullptr);
  if (result != ESP_OK) {
    return result;
  }
  parlio_rx_soft_delimiter_config_t delimiter_config = {};
  delimiter_config.sample_edge = PARLIO_SAMPLE_EDGE_POS;
  delimiter_config.bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB;
  delimiter_config.eof_data_len = kDelimiterSize;
  return parlio_new_rx_soft_delimiter(&delimiter_config, delimiter);
}

static size_t fifo_used(void) {
  return fifo.head - fifo.tail;
}

// Returns what it could not take. A short write means the consumer has fallen
// behind, which is the condition this experiment is looking for.
static size_t fifo_push(const uint8_t *data, size_t length) {
  size_t free_space = kFifoSize - fifo_used();
  if (free_space < length) {
    length = free_space;
  }
  size_t offset = fifo.head % kFifoSize;
  const size_t first = (kFifoSize - offset) < length ? (kFifoSize - offset) : length;
  memcpy(fifo.buffer + offset, data, first);
  if (length > first) {
    memcpy(fifo.buffer, data + first, length - first);
  }
  fifo.head += length;
  const size_t used = fifo_used();
  if (used > fifo.high_water) {
    fifo.high_water = used;
  }
  return length;
}

static void harvest_task(void *) {
  while (stream_active && harvested < stream_target) {
    Chunk chunk = {};
    if (xQueueReceive(chunk_queue, &chunk, pdMS_TO_TICKS(200)) != pdTRUE) {
      continue;
    }
    size_t offset = 0;
    while (offset < chunk.length) {
      const size_t taken = fifo_push(chunk.data + offset, chunk.length - offset);
      if (taken == 0) {
        ++fifo.overflow;
        break;  // the FIFO is full: the drop is the measurement, do not stall the ring
      }
      offset += taken;
      harvested += taken;
    }
  }
  xSemaphoreGive(harvest_done);
  vTaskDelete(nullptr);
}

// waitWritable() instead of spinning on write(): the point of this experiment is
// that the sender shares the CPU with harvest, and a task that spins is a task
// that takes it away. The wait is for a slice, not for the whole chunk --
// waitWritable() clamps to the FIFO size, so asking for kUsbChunk would mean
// "wait until the FIFO is completely empty" and give the drain away again.
static void usb_task(void *) {
  size_t sent = 0;
  uint32_t stalls = 0;
  uint32_t waits = 0;
  uint32_t timeouts = 0;
  const uint64_t started = esp_timer_get_time();
  while (sent < stream_target) {
    const size_t used = fifo_used();
    if (used == 0) {
      if (!stream_active && harvested >= stream_target) {
        break;
      }
      taskYIELD();
      continue;
    }
    const size_t offset = fifo.tail % kFifoSize;
    size_t span = (kFifoSize - offset) < used ? (kFifoSize - offset) : used;
    if (span > kUsbChunk) {
      span = kUsbChunk;
    }
    if (span > stream_target - sent) {
      span = stream_target - sent;
    }
    // Look before waiting: on most passes there is already room, and then the
    // semaphore is never touched.
    if (HsVendor.writeAvailable() < kWaitSlice) {
      ++waits;
      if (!HsVendor.waitWritable(kWaitSlice, kWaitTimeoutMs)) {
        ++timeouts;
        if (!HsVendor.mounted()) {
          break;  // unplugged: do not spin here forever
        }
        continue;
      }
    }
    const size_t written = HsVendor.write(fifo.buffer + offset, span);
    if (written == 0) {
      ++stalls;
      taskYIELD();
      continue;
    }
    fifo.tail += written;
    sent += written;
  }
  HsVendor.flush();
  usb_elapsed_us = esp_timer_get_time() - started;
  usb_sent = sent;
  usb_stalls = stalls;
  usb_waits = waits;
  usb_timeouts = timeouts;
  xSemaphoreGive(usb_done);
  vTaskDelete(nullptr);
}

static void run_stream(size_t total_bytes, uint32_t rate_hz) {
  fifo.head = 0;
  fifo.tail = 0;
  fifo.high_water = 0;
  fifo.overflow = 0;
  ring_overflow = 0;
  callback_bytes = 0;
  harvested = 0;
  usb_sent = 0;
  usb_stalls = 0;
  usb_waits = 0;
  usb_timeouts = 0;
  usb_elapsed_us = 0;
  stream_target = total_bytes;
  stream_active = true;
  xQueueReset(chunk_queue);
  memset(ring_buffer, 0xA5, kRingSize);
  esp_cache_msync(ring_buffer, kRingSize, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

  parlio_rx_unit_handle_t rx_unit = nullptr;
  parlio_rx_delimiter_handle_t delimiter = nullptr;
  esp_err_t status = create_receiver(&rx_unit, &delimiter, rate_hz);
  if (status == ESP_OK) {
    status = configure_pwm();
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

  if (status != ESP_OK) {
    Console.printf("STREAM status=%d rate_hz=%lu\n", static_cast<int>(status), static_cast<unsigned long>(rate_hz));
    Console.flush();
    stream_active = false;
    cleanup_pwm();
    return;
  }

  Console.printf("STREAM bytes=%lu rate_hz=%lu\n", static_cast<unsigned long>(total_bytes),
                 static_cast<unsigned long>(rate_hz));
  Console.flush();

  const uint64_t started = esp_timer_get_time();
  xTaskCreatePinnedToCore(usb_task, "e078_tx", 4096, nullptr, 5, nullptr, kUsbCore);
  xTaskCreatePinnedToCore(harvest_task, "e078_hv", 4096, nullptr, 5, nullptr, kHarvestCore);
  parlio_rx_soft_delimiter_start_stop(rx_unit, delimiter, true);

  xSemaphoreTake(usb_done, portMAX_DELAY);
  stream_active = false;
  xSemaphoreTake(harvest_done, portMAX_DELAY);
  const uint64_t elapsed = esp_timer_get_time() - started;

  parlio_rx_soft_delimiter_start_stop(rx_unit, delimiter, false);
  parlio_rx_unit_disable(rx_unit);
  parlio_del_rx_delimiter(delimiter);
  parlio_del_rx_unit(rx_unit);
  cleanup_pwm();

  Console.printf(
    "DONE sent=%lu harvested=%lu ring_overflow=%lu fifo_overflow=%lu high_water=%lu stalls=%lu waits=%lu "
    "timeouts=%lu elapsed_us=%llu\n",
    static_cast<unsigned long>(usb_sent), static_cast<unsigned long>(harvested),
    static_cast<unsigned long>(ring_overflow), static_cast<unsigned long>(fifo.overflow),
    static_cast<unsigned long>(fifo.high_water), static_cast<unsigned long>(usb_stalls),
    static_cast<unsigned long>(usb_waits), static_cast<unsigned long>(usb_timeouts),
    static_cast<unsigned long long>(elapsed)
  );
  Console.flush();
}

static void handle_command(void) {
  if (command[0] == '?') {
    Console.printf("# EXP E078 v1 git=%s probe=esp32p4_parlio target=internal build=%s %s\n", BANNER_GIT, __DATE__,
                   __TIME__);
    Console.printf(
      "ENV chip=%s psram_found=%u psram_size=%lu lanes=%u pins=%d,%d pwm_hz=%lu fifo=%lu ring=%lu stream_max=%lu "
      "tx_fifo=%lu usb_ready=%u mounted=%u ready=%u\n",
      ESP.getChipModel(), psramFound() ? 1U : 0U, static_cast<unsigned long>(ESP.getPsramSize()), kLaneCount, pins[0],
      pins[1], static_cast<unsigned long>(kPwmFrequencyHz), static_cast<unsigned long>(kFifoSize),
      static_cast<unsigned long>(kRingSize), static_cast<unsigned long>(kStreamBytesMax),
      static_cast<unsigned long>(EspUsbDeviceVendor::writeCapacity()), usb_ready ? 1U : 0U,
      HsVendor.mounted() ? 1U : 0U, (fifo.buffer && ring_buffer) ? 1U : 0U
    );
    Console.flush();
    return;
  }
  if (command[0] == 'S') {
    unsigned long bytes = 0;
    unsigned long rate = 0;
    if (sscanf(command + 1, "%lu %lu", &bytes, &rate) != 2 || bytes == 0 || bytes > kStreamBytesMax || rate == 0) {
      Console.printf("STREAM status=reject bytes=%lu rate_hz=%lu\n", bytes, rate);
      Console.flush();
      return;
    }
    run_stream(bytes, static_cast<uint32_t>(rate));
    return;
  }
  Console.println("CFG status=unknown");
  Console.flush();
}

void setup() {
  Console.begin();
  parse_pins();
  chunk_queue = xQueueCreate(kQueueDepth, sizeof(Chunk));
  ring_buffer = static_cast<uint8_t *>(heap_caps_aligned_alloc(64, kRingSize, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  fifo.buffer = static_cast<uint8_t *>(heap_caps_malloc(kFifoSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  usb_done = xSemaphoreCreateBinary();
  harvest_done = xSemaphoreCreateBinary();

  EspUsbDeviceConfig config;
  config.vid = kTestVid;
  config.pid = kTestPid;
  config.manufacturer = "Open Embedded Probe (TEST ONLY)";
  config.product = "OEP P4 Continuous Stream";
  // Same identity as E069/E071/E076 so usbipd's existing bind still applies.
  config.serialNumber = "E069-A";
  config.selfPowered = true;
  config.maxPowerMilliamps = 500;
  config.webusbEnabled = true;
  config.controller = EspUsbController::HighSpeed;
  usb_ready = device.begin(config);
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
    Console.println("READY E078");
    Console.flush();
    host_armed = true;
    return;
  }
  if (!Console.available()) {
    delay(1);
    return;
  }
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
