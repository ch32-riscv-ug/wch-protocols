// E109: E108 firmware plus DWC2 register readout (GAHBCFG, GINTMSK) for soak /
// path-difference runs. The data path is unchanged from E108.
//
// The codec (core 1) writes wire bytes straight into 27,136-byte stages, and the
// USB side (core 0) hands whole stages to the DWC2 endpoint without copying.
// The PSRAM FIFO only absorbs data while USB is behind; it is drained through
// two bounce buffers in 512-byte multiples. Every transfer is one stage or one
// bounce, so transfer lengths are multiples of 512 except the final one.
//
// Requires EspUsbDevice 2.3.0 with espusbdevice-e108.patch (non-buffered vendor
// class, direct RX / TX-complete hooks, zero-copy vendord_ep_write).
//
// Codec profiles are unchanged from E106 / E107:
//   8-bit:          3 raw + 5 hold D=64          (64 samples -> 25 bytes)
//   16-bit legacy:  3 raw + 8 hold D=64          (64 samples -> 25 bytes)
//   16-bit wide:    3 raw + 1 D=8 + 12 D=64      (128 samples -> 53 bytes)

#include <Arduino.h>
#include "EspUsbDevice.h"

#include <driver/gpio.h>
#include <driver/parlio_rx.h>
#include <driver/parlio_tx.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#ifndef E108_CODEC_O2
#define E108_CODEC_O2 0
#endif
#ifndef E108_CODEC_PREFETCH
#define E108_CODEC_PREFETCH 0
#endif

namespace {

constexpr uint32_t kSampleRateHz = 32000000;
constexpr size_t kSourceLanes = 8;
constexpr size_t kLegacyBlockSamples = 64;
constexpr size_t kLegacyWireBlockBytes = 25;
constexpr size_t kWideBlockSamples = 128;
constexpr size_t kWideWireBlockBytes = 53;
constexpr size_t kMaxBlockSamples = kWideBlockSamples;
constexpr size_t kMaxWireBlockBytes = kWideWireBlockBytes;
constexpr size_t kRingBytes = 64 * 1024;
constexpr size_t kDelimiterBytes = 65408;
constexpr size_t kQueueDepth = 128;
constexpr size_t kSourceBytes = 8192;
constexpr uint32_t kSourceDivider = 4;
// One stage is one USB transfer. 27,136 = lcm(53, 512): the wide profile fills
// 512 blocks, the 25-byte profiles fill 1,024 blocks (25,600 B). Both are
// multiples of 512 (whole packets) and of 64 (cache lines).
constexpr size_t kStageBytes = 27136;
constexpr size_t kStageFillWide = 512 * kWideWireBlockBytes;
constexpr size_t kStageFillLegacy = 1024 * kLegacyWireBlockBytes;
constexpr size_t kStageCount = 4;
constexpr size_t kBounceCount = 2;
constexpr size_t kSpillBytes = 8 * 1024 * 1024;
constexpr size_t kArmDepth = 4;
// At most this many stages may sit in the arm ring / in flight; the rest stay
// with the codec so it never starves while USB is the slower side.
constexpr size_t kDirectDepth = 2;
constexpr size_t kStatusBytes = 1024;
constexpr size_t kCommandBytes = 16;
constexpr uint32_t kStatusWaitMs = 2000;
constexpr uint16_t kVid = 0x303a;
constexpr uint16_t kPid = 0x4021;
constexpr int kPins[kSourceLanes] = {2, 3, 4, 5, 6, 7, 8, 9};
constexpr int kUsbCore = 0;  // E107: the DWC2 ISR and usbd task follow Device.begin()

constexpr uint8_t kFlagNoCheck = 0x01;    // skip the device-side Gray check
constexpr uint8_t kFlagNoDirect = 0x02;   // every stage through the PSRAM spill path
constexpr uint8_t kFlagLoadProbe = 0x04;  // priority-1 spin task on core 0 measures ISR-inclusive free time
// DWC2 HS controller registers (dwc2_esp32.h: DWC2_HS_REG_BASE). GAHBCFG.DMAEn and
// GHWCFG2.arch tell whether the DCD really runs the controller in DMA mode.
constexpr uintptr_t kDwc2HsBase = 0x50000000UL;

enum SegmentKind : uint8_t { kSegStage, kSegBounce, kSegProbe, kSegStatus };

struct Segment {
  const uint8_t *data;
  uint32_t length;
  uint8_t kind;
  uint8_t slot;
};

struct Chunk {
  const uint8_t *data;
  size_t length;
};

struct WireChunk {
  uint8_t *data;
  size_t length;
};

struct StatSnapshot {
  uint32_t idle0;
  uint32_t idle1;
  uint32_t usbd;
  int64_t wall;
};

EspUsbDevice Device;
EspUsbDeviceVendor Vendor(Device, 512);
EspUsbDeviceConfig UsbConfig;
QueueHandle_t ChunkQueue;
QueueHandle_t FreeStageQueue;
QueueHandle_t ReadyStageQueue;
SemaphoreHandle_t HarvestDone;
SemaphoreHandle_t UsbDone;
SemaphoreHandle_t UsbWake;
SemaphoreHandle_t StatusDone;
SemaphoreHandle_t UsbBeginDone;
uint8_t *Ring;
uint8_t *Source;
uint8_t *Spill;
uint16_t Pending[kMaxBlockSamples] __attribute__((aligned(64)));
uint8_t BenchWire[kMaxWireBlockBytes] __attribute__((aligned(64)));
uint8_t Stage[kStageCount][kStageBytes] __attribute__((aligned(64)));
uint8_t Bounce[kBounceCount][kStageBytes] __attribute__((aligned(64)));
uint8_t StatusBuffer[kStatusBytes] __attribute__((aligned(64)));

// Spill FIFO (PSRAM): head written by the usb task, tail read by the usb task.
volatile size_t SpillHead;
volatile size_t SpillTail;
volatile size_t SpillHighWater;
volatile uint32_t SpillOverflow;
volatile uint8_t BounceBusy[kBounceCount];

// Arm ring: pushed by the usb task, popped by whoever arms next (usb task or
// the TX-complete callback on the usbd task). One segment is in flight at a time.
Segment ArmRing[kArmDepth];
volatile uint32_t ArmHead;
volatile uint32_t ArmTail;
volatile bool Inflight;
Segment InflightSegment;
portMUX_TYPE ArmMux = portMUX_INITIALIZER_UNLOCKED;

volatile bool Active;
volatile bool CodecFinished;
volatile uint64_t TargetBlocks;
volatile uint64_t EncodedBlocks;
volatile uint64_t SentBytes;
volatile uint64_t Completions;
volatile uint64_t DirectSegments;
volatile uint64_t BounceSegments;
volatile uint64_t SpillBytes;
volatile uint32_t ArmFailures;
volatile uint64_t CallbackBytes;
volatile uint32_t QueueOverflow;
volatile int64_t CaptureUs;
volatile int64_t UsbUs;
volatile uint64_t ProcessUs;
volatile uint32_t HarvestChunks;
volatile uint32_t RawSequenceBad;
volatile uint32_t DuplicateBad;
volatile size_t MaxInflight;
volatile uint64_t SinkBytes;
volatile uint32_t SinkChecksum;
volatile uint32_t UsbTaskUs;
volatile uint32_t CodecTaskUs;
volatile int RxIsrCore = -1;
uint32_t Idle0Us;
uint32_t Idle1Us;
uint32_t UsbdUs;
int64_t StatWallUs;
int UsbInitCore = -1;
TaskHandle_t UsbdTask;
UBaseType_t UsbdPriority;
uint64_t BenchUs;
uint32_t BenchChecksum;
bool UsbReady;
volatile bool RunPending;
volatile bool CommandPending;
uint8_t CommandMailbox[kCommandBytes];
char CommandMode;
uint32_t RequestedRateHz = kSampleRateHz;
uint8_t RequestedWidth = 16;
uint8_t Flags;
bool SinkOnly;
bool WideProfile;
volatile uint64_t ProbeRemaining;
volatile bool SpinRun;
volatile uint32_t SpinCount;
SemaphoreHandle_t SpinDone;
uint32_t SpinCalibPerSec;
int64_t SpinUs;
uint32_t SpinResult;

static uint32_t dwc2Register(uint32_t offset) {
  return *reinterpret_cast<volatile uint32_t *>(kDwc2HsBase + offset);
}
static unsigned dwc2DmaActive() { return (dwc2Register(0x08) >> 5) & 1U; }
// GINTMSK.RXFLVL (bit 4) is set only by the slave-mode branch of dwc2_core_init().
static unsigned dwc2RxflvlMasked() { return (dwc2Register(0x18) >> 4) & 1U; }
static unsigned dwc2Arch() { return (dwc2Register(0x48) >> 3) & 3U; }

// Idle replacement: everything that runs on core 0 above priority 1 - tasks and
// interrupts alike - steals iterations from this loop, so its count against a
// calibrated rate is the core's ISR-inclusive free fraction.
static void spinTask(void *) {
  uint32_t n = 0;
  while (SpinRun) {
    ++n;
    if ((n & 0x3ff) == 0) SpinCount = n;
  }
  SpinCount = n;
  xSemaphoreGive(SpinDone);
  vTaskDelete(nullptr);
}

static void spinStart() {
  SpinRun = true;
  SpinCount = 0;
  xTaskCreatePinnedToCore(spinTask, "e108_spin", 2048, nullptr, 1, nullptr, 0);
}

static uint32_t spinStop() {
  SpinRun = false;
  xSemaphoreTake(SpinDone, pdMS_TO_TICKS(200));
  return SpinCount;
}

static size_t activeWireBlockBytes() {
  return WideProfile ? kWideWireBlockBytes : kLegacyWireBlockBytes;
}

static uint32_t taskRunTime(TaskHandle_t task) {
  TaskStatus_t status = {};
  vTaskGetInfo(task, &status, pdFALSE, eInvalid);
  return status.ulRunTimeCounter;
}

static StatSnapshot snapshotStats() {
  StatSnapshot s;
  s.idle0 = ulTaskGetIdleRunTimeCounterForCore(0);
  s.idle1 = ulTaskGetIdleRunTimeCounterForCore(1);
  s.usbd = UsbdTask ? taskRunTime(UsbdTask) : 0;
  s.wall = esp_timer_get_time();
  return s;
}

// ---- codec ----------------------------------------------------------------
#if E108_CODEC_O2
#pragma GCC push_options
#pragma GCC optimize("O2")
#endif

static inline __attribute__((always_inline)) uint32_t gatherLow3x4Bytes(uint32_t w) {
  w &= 0x07070707U;
  w = (w | (w >> 5)) & 0x003f003fU;
  return (w | (w >> 10)) & 0x0fffU;
}

static inline __attribute__((always_inline)) uint32_t gatherLow3x4Halves(uint32_t w0, uint32_t w1) {
  const uint32_t x = (w0 & 0x00070007U) | ((w1 & 0x00070007U) << 6);
  return (x | (x >> 13)) & 0x0fffU;
}

static inline __attribute__((always_inline)) void store24(uint8_t *out, uint32_t packed) {
  out[0] = static_cast<uint8_t>(packed);
  out[1] = static_cast<uint8_t>(packed >> 8);
  out[2] = static_cast<uint8_t>(packed >> 16);
}

static inline __attribute__((always_inline)) uint32_t spread12(uint32_t value) {
  value &= 0x00000fffU;
  value = (value | (value << 8)) & 0x00ff00ffU;
  value = (value | (value << 4)) & 0x0f0f0f0fU;
  value = (value | (value << 2)) & 0x33333333U;
  value = (value | (value << 1)) & 0x55555555U;
  return value;
}

static inline __attribute__((always_inline)) void encodeBlock(
    const uint16_t *samples, uint8_t *wire) {
  const auto *words = reinterpret_cast<const uint32_t *>(samples);
  for (size_t group = 0; group < 8; ++group) {
    const uint32_t *w = words + group * 4;
    store24(wire + group * 3,
            gatherLow3x4Halves(w[0], w[1]) | (gatherLow3x4Halves(w[2], w[3]) << 12));
  }
  wire[24] = static_cast<uint8_t>(words[0] >> 3);
}

static inline __attribute__((always_inline)) void encodeBlock8(
    const uint8_t *samples, uint8_t *wire) {
  const auto *words = reinterpret_cast<const uint32_t *>(samples);
  for (size_t group = 0; group < 8; ++group) {
    const uint32_t *w = words + group * 2;
    store24(wire + group * 3, gatherLow3x4Bytes(w[0]) | (gatherLow3x4Bytes(w[1]) << 12));
  }
  wire[24] = static_cast<uint8_t>((words[0] >> 3) & 0x1f);
}

static inline __attribute__((always_inline)) void encodeBlockWide(
    const uint16_t *samples, uint8_t *wire) {
  const auto *words = reinterpret_cast<const uint32_t *>(samples);
  uint32_t d8 = 0;
  for (size_t group = 0; group < 16; ++group) {
    const uint32_t *w = words + group * 4;
    store24(wire + group * 3,
            gatherLow3x4Halves(w[0], w[1]) | (gatherLow3x4Halves(w[2], w[3]) << 12));
    d8 |= ((w[0] >> 3) & 1U) << group;
  }
  wire[48] = static_cast<uint8_t>(d8);
  wire[49] = static_cast<uint8_t>(d8 >> 8);
  const uint32_t d64 = spread12((words[0] & 0xffffU) >> 4) |
      (spread12((words[32] & 0xffffU) >> 4) << 1);
  store24(wire + 50, d64);
}

enum class Profile { Legacy16, Eight, Wide };

template <Profile P, bool kCheck>
static void harvestBody() {
  constexpr size_t kSamples = P == Profile::Wide ? kWideBlockSamples : kLegacyBlockSamples;
  constexpr size_t kRaw = kSamples * (P == Profile::Eight ? 1 : 2);
  constexpr size_t kWire = P == Profile::Wide ? kWideWireBlockBytes : kLegacyWireBlockBytes;
  constexpr size_t kFill = P == Profile::Wide ? kStageFillWide : kStageFillLegacy;
  constexpr uint8_t kExpectedDelta = kSamples / 4;
  constexpr uint8_t kDeltaMargin = kSamples / 64;
  uint8_t *stage = nullptr;
  xQueueReceive(FreeStageQueue, &stage, portMAX_DELAY);
  size_t staged = 0;
  size_t pending = 0;
  uint32_t idle = 0;
  uint64_t encoded = 0;
  const uint64_t target = TargetBlocks;
  const int64_t taskBegan = esp_timer_get_time();
  bool previousValid = false;
  uint8_t previousBinary = 0;
  uint32_t rawSequenceBad = 0;
  uint32_t duplicateBad = 0;
  size_t maxInflight = 0;
  uint8_t *const pendingBytes = reinterpret_cast<uint8_t *>(Pending);
  const auto check = [&](const uint8_t *bytes) __attribute__((always_inline)) {
    if (!kCheck) return;
    const uint8_t low = bytes[0];
    if (P != Profile::Eight && bytes[1] != low) ++duplicateBad;
    uint8_t binary = low;
    binary ^= binary >> 1;
    binary ^= binary >> 2;
    binary ^= binary >> 4;
    if (previousValid) {
      const uint8_t delta = static_cast<uint8_t>(binary - previousBinary);
      if (delta < kExpectedDelta - kDeltaMargin || delta > kExpectedDelta + kDeltaMargin) ++rawSequenceBad;
    }
    previousBinary = binary;
    previousValid = true;
  };
  const auto encode = [](const uint8_t *in, uint8_t *out) __attribute__((always_inline)) {
    if constexpr (P == Profile::Wide) encodeBlockWide(reinterpret_cast<const uint16_t *>(in), out);
    else if constexpr (P == Profile::Eight) encodeBlock8(in, out);
    else encodeBlock(reinterpret_cast<const uint16_t *>(in), out);
  };
  const auto publish = [&]() {
    // The DWC2 (DMA build) reads memory, not this core's L1: write the stage
    // back before it leaves the codec. Lengths are rounded to whole lines; the
    // stage array is line-sized so the final partial stage stays in bounds.
    const size_t lines = (staged + 63) & ~size_t(63);
    esp_cache_msync(stage, lines, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    const WireChunk ready = {stage, staged};
    xQueueSend(ReadyStageQueue, &ready, portMAX_DELAY);
  };
  const auto rotate = [&]() {
    publish();
    xQueueReceive(FreeStageQueue, &stage, portMAX_DELAY);
    staged = 0;
  };
  while (encoded < target) {
    Chunk chunk = {};
    if (xQueueReceive(ChunkQueue, &chunk, pdMS_TO_TICKS(200)) != pdTRUE) {
      if (++idle >= 10) break;
      continue;
    }
    idle = 0;
    size_t at = 0;
    if (pending != 0) {
      const size_t take = min(kRaw - pending, chunk.length);
      memcpy(pendingBytes + pending, chunk.data, take);
      pending += take;
      at += take;
      if (pending == kRaw) {
        if (staged + kWire > kFill) rotate();
        check(pendingBytes);
        encode(pendingBytes, stage + staged);
        staged += kWire;
        ++encoded;
        pending = 0;
      }
    }
    while (at + kRaw <= chunk.length && encoded < target) {
      if (staged + kWire > kFill) rotate();
      size_t n = (chunk.length - at) / kRaw;
      n = min(n, (kFill - staged) / kWire);
      const uint64_t remaining = target - encoded;
      if (remaining < n) n = static_cast<size_t>(remaining);
      const uint8_t *in = chunk.data + at;
      uint8_t *out = stage + staged;
      for (size_t i = 0; i < n; ++i, in += kRaw, out += kWire) {
#if E108_CODEC_PREFETCH
        // Touch every cache line of the next block before encoding this one, so
        // the invalidated DMA-ring lines are already in flight when they are used.
        if (i + 1 < n) {
          for (size_t line = 0; line < kRaw; line += 64) {
            const uint32_t touched = *reinterpret_cast<const volatile uint32_t *>(in + kRaw + line);
            asm volatile("" : : "r"(touched));
          }
        }
#endif
        check(in);
        encode(in, out);
      }
      at += n * kRaw;
      staged += n * kWire;
      encoded += n;
    }
    if (at < chunk.length && encoded < target) {
      const size_t take = chunk.length - at;
      memcpy(pendingBytes, chunk.data + at, take);
      pending = take;
    }
    ++HarvestChunks;
    const size_t processedRaw = static_cast<size_t>(encoded * kRaw + pending);
    const size_t callback = static_cast<size_t>(CallbackBytes);
    const size_t inflight = callback > processedRaw ? callback - processedRaw : 0;
    if (inflight > maxInflight) maxInflight = inflight;
  }
  if (staged != 0) {
    publish();
  } else {
    xQueueSend(FreeStageQueue, &stage, portMAX_DELAY);
  }
  ProcessUs = esp_timer_get_time() - taskBegan;
  EncodedBlocks = encoded;
  RawSequenceBad = rawSequenceBad;
  DuplicateBad = duplicateBad;
  MaxInflight = maxInflight;
  CodecTaskUs = taskRunTime(nullptr);
}

#if E108_CODEC_O2
#pragma GCC pop_options
#endif

static void harvestTask(void *) {
  const bool check = (Flags & kFlagNoCheck) == 0;
  if (WideProfile) check ? harvestBody<Profile::Wide, true>() : harvestBody<Profile::Wide, false>();
  else if (RequestedWidth == 8) check ? harvestBody<Profile::Eight, true>() : harvestBody<Profile::Eight, false>();
  else check ? harvestBody<Profile::Legacy16, true>() : harvestBody<Profile::Legacy16, false>();
  xSemaphoreGive(HarvestDone);
  vTaskDelete(nullptr);
}

// ---- USB return path -------------------------------------------------------

static uint32_t armQueued() {
  portENTER_CRITICAL(&ArmMux);
  const uint32_t queued = ArmTail - ArmHead + (Inflight ? 1U : 0U);
  portEXIT_CRITICAL(&ArmMux);
  return queued;
}

// Arm the next segment if nothing is in flight. Safe from the usb task and from
// the TX-complete callback (usbd task); the spinlock decides who claims.
static void tryArm() {
  Segment seg = {};
  bool claimed = false;
  portENTER_CRITICAL(&ArmMux);
  if (!Inflight && ArmHead != ArmTail) {
    seg = ArmRing[ArmHead % kArmDepth];
    ++ArmHead;
    Inflight = true;
    InflightSegment = seg;
    claimed = true;
  }
  portEXIT_CRITICAL(&ArmMux);
  if (!claimed) return;
  // Non-buffered + zero-copy: this is usbd_edpt_xfer() on our buffer, no copy.
  const size_t wrote = Vendor.write(seg.data, seg.length);
  if (wrote != seg.length) {
    ++ArmFailures;
    portENTER_CRITICAL(&ArmMux);
    Inflight = false;
    portEXIT_CRITICAL(&ArmMux);
  }
}

static bool armPush(const Segment &seg) {
  bool ok = false;
  portENTER_CRITICAL(&ArmMux);
  if (ArmTail - ArmHead < kArmDepth) {
    ArmRing[ArmTail % kArmDepth] = seg;
    ++ArmTail;
    ok = true;
  }
  portEXIT_CRITICAL(&ArmMux);
  if (ok) tryArm();
  return ok;
}

static void armReset() {
  portENTER_CRITICAL(&ArmMux);
  ArmHead = ArmTail = 0;
  Inflight = false;
  portEXIT_CRITICAL(&ArmMux);
}

static size_t spillUsed() { return SpillHead - SpillTail; }

static void spillPush(const uint8_t *data, size_t length) {
  if (kSpillBytes - spillUsed() < length) {
    ++SpillOverflow;  // dropped; the host sees the shortfall
    return;
  }
  const size_t offset = SpillHead % kSpillBytes;
  const size_t first = min(length, kSpillBytes - offset);
  memcpy(Spill + offset, data, first);
  if (length > first) memcpy(Spill, data + first, length - first);
  SpillHead += length;
  SpillBytes += length;
  const size_t used = spillUsed();
  if (used > SpillHighWater) SpillHighWater = used;
}

static void spillPop(uint8_t *out, size_t length) {
  const size_t offset = SpillTail % kSpillBytes;
  const size_t first = min(length, kSpillBytes - offset);
  memcpy(out, Spill + offset, first);
  if (length > first) memcpy(out + first, Spill, length - first);
  SpillTail += length;
}

// Move spilled bytes into free bounce buffers and queue them, in order.
static void refillBounces(bool final) {
  for (size_t b = 0; b < kBounceCount; ++b) {
    if (BounceBusy[b]) continue;
    const size_t used = spillUsed();
    if (used == 0 || armQueued() >= kArmDepth) return;
    size_t length = min(kStageBytes, used);
    if (!final) {
      length &= ~size_t(511);
      if (length == 0) return;
    }
    spillPop(Bounce[b], length);
    BounceBusy[b] = 1;
    const Segment seg = {Bounce[b], static_cast<uint32_t>(length), kSegBounce, static_cast<uint8_t>(b)};
    armPush(seg);
    ++BounceSegments;
  }
}

static uint8_t stageSlot(const uint8_t *data) {
  for (size_t i = 0; i < kStageCount; ++i) {
    if (data == Stage[i]) return static_cast<uint8_t>(i);
  }
  return 0xff;
}

static void usbTask(void *) {
  const int64_t began = esp_timer_get_time();
  while (true) {
    WireChunk ready = {};
    if (xQueueReceive(ReadyStageQueue, &ready, pdMS_TO_TICKS(1)) == pdTRUE) {
      const bool direct = (Flags & kFlagNoDirect) == 0 && spillUsed() == 0 && armQueued() < kDirectDepth;
      if (direct) {
        const Segment seg = {ready.data, static_cast<uint32_t>(ready.length), kSegStage, stageSlot(ready.data)};
        armPush(seg);
        ++DirectSegments;
      } else {
        spillPush(ready.data, ready.length);
        uint8_t *slot = ready.data;
        xQueueSend(FreeStageQueue, &slot, portMAX_DELAY);
      }
    }
    const bool drained = CodecFinished && uxQueueMessagesWaiting(ReadyStageQueue) == 0;
    refillBounces(drained);
    if (drained && spillUsed() == 0 && armQueued() == 0) break;
  }
  UsbUs = esp_timer_get_time() - began;
  UsbTaskUs = taskRunTime(nullptr);
  xSemaphoreGive(UsbDone);
  vTaskDelete(nullptr);
}

// Internal-sink mode: return stages without sending, so the codec alone is measured.
static void sinkTask(void *) {
  while (true) {
    WireChunk ready = {};
    if (xQueueReceive(ReadyStageQueue, &ready, pdMS_TO_TICKS(1)) == pdTRUE) {
      uint32_t checksum = SinkChecksum;
      for (size_t i = 0; i < ready.length; i += 64) checksum += ready.data[i];
      SinkChecksum = checksum;
      SinkBytes += ready.length;
      uint8_t *slot = ready.data;
      xQueueSend(FreeStageQueue, &slot, portMAX_DELAY);
      continue;
    }
    if (CodecFinished && uxQueueMessagesWaiting(ReadyStageQueue) == 0) break;
  }
  UsbTaskUs = taskRunTime(nullptr);
  xSemaphoreGive(UsbDone);
  vTaskDelete(nullptr);
}

}  // namespace

// TinyUSB completion (usbd task, core kUsbCore): release the segment, chain the next.
extern "C" void esp_usb_device_direct_vendor_tx_complete(uint32_t sentBytes) {
  Segment seg = {};
  bool had = false;
  portENTER_CRITICAL(&ArmMux);
  if (Inflight) {
    seg = InflightSegment;
    Inflight = false;
    had = true;
  }
  portEXIT_CRITICAL(&ArmMux);
  if (!had) return;
  SentBytes += sentBytes;
  ++Completions;
  switch (seg.kind) {
    case kSegStage: {
      uint8_t *slot = Stage[seg.slot];
      xQueueSend(FreeStageQueue, &slot, 0);
      break;
    }
    case kSegBounce:
      BounceBusy[seg.slot] = 0;
      break;
    case kSegStatus:
      xSemaphoreGive(StatusDone);
      break;
    default:
      break;
  }
  tryArm();
  if (UsbWake) xSemaphoreGive(UsbWake);
}

// Non-buffered vendor RX (usbd task): commands are exactly 16 bytes.
extern "C" void esp_usb_device_direct_vendor_rx(const uint8_t *buffer, uint32_t length) {
  if (length < kCommandBytes || CommandPending) return;
  memcpy(CommandMailbox, buffer, kCommandBytes);
  CommandPending = true;
}

namespace {

static bool IRAM_ATTR onPartialReceive(
    parlio_rx_unit_handle_t, const parlio_rx_event_data_t *event, void *) {
  const Chunk chunk = {static_cast<const uint8_t *>(event->data), event->recv_bytes};
  CallbackBytes += event->recv_bytes;
  RxIsrCore = xPortGetCoreID();
  BaseType_t wake = pdFALSE;
  if (xQueueSendFromISR(ChunkQueue, &chunk, &wake) != pdTRUE) ++QueueOverflow;
  return wake == pdTRUE;
}

static esp_err_t createSource(parlio_tx_unit_handle_t *unit, uint32_t sampleRateHz) {
  parlio_tx_unit_config_t c = {};
  c.clk_src = PARLIO_CLK_SRC_DEFAULT;
  c.clk_in_gpio_num = GPIO_NUM_NC;
  c.output_clk_freq_hz = sampleRateHz / kSourceDivider;
  c.data_width = kSourceLanes;
  for (size_t i = 0; i < PARLIO_TX_UNIT_MAX_DATA_WIDTH; ++i) {
    c.data_gpio_nums[i] = i < kSourceLanes ? gpio_num_t(kPins[i]) : GPIO_NUM_NC;
  }
  c.clk_out_gpio_num = GPIO_NUM_NC;
  c.valid_gpio_num = GPIO_NUM_NC;
  c.trans_queue_depth = 1;
  c.max_transfer_size = kSourceBytes;
  c.sample_edge = PARLIO_SAMPLE_EDGE_NEG;
  c.bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB;
  c.flags.clk_gate_en = false;
  return parlio_new_tx_unit(&c, unit);
}

static esp_err_t createReceiver(parlio_rx_unit_handle_t *unit,
                                parlio_rx_delimiter_handle_t *delimiter,
                                uint32_t sampleRateHz, uint8_t captureWidth) {
  parlio_rx_unit_config_t c = {};
  c.trans_queue_depth = 1;
  c.max_recv_size = kRingBytes;
  c.data_width = captureWidth;
  c.clk_src = PARLIO_CLK_SRC_DEFAULT;
  c.exp_clk_freq_hz = sampleRateHz;
  c.clk_in_gpio_num = GPIO_NUM_NC;
  c.clk_out_gpio_num = GPIO_NUM_NC;
  c.valid_gpio_num = GPIO_NUM_NC;
  for (size_t i = 0; i < PARLIO_RX_UNIT_MAX_DATA_WIDTH; ++i) {
    c.data_gpio_nums[i] = i < captureWidth ? gpio_num_t(kPins[i % kSourceLanes]) : GPIO_NUM_NC;
  }
  esp_err_t result = parlio_new_rx_unit(&c, unit);
  if (result != ESP_OK) return result;
  parlio_rx_event_callbacks_t callbacks = {};
  callbacks.on_partial_receive = onPartialReceive;
  result = parlio_rx_unit_register_event_callbacks(*unit, &callbacks, nullptr);
  if (result != ESP_OK) return result;
  parlio_rx_soft_delimiter_config_t d = {};
  d.sample_edge = PARLIO_SAMPLE_EDGE_POS;
  d.bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB;
  d.eof_data_len = kDelimiterBytes;
  return parlio_new_rx_soft_delimiter(&d, delimiter);
}

static void resetRunState() {
  EncodedBlocks = SentBytes = CallbackBytes = 0;
  Completions = DirectSegments = BounceSegments = SpillBytes = 0;
  ArmFailures = QueueOverflow = 0;
  CaptureUs = UsbUs = 0;
  ProcessUs = 0;
  HarvestChunks = 0;
  RawSequenceBad = DuplicateBad = 0;
  MaxInflight = 0;
  SinkBytes = SinkChecksum = 0;
  UsbTaskUs = CodecTaskUs = 0;
  Idle0Us = Idle1Us = UsbdUs = 0;
  StatWallUs = 0;
  RxIsrCore = -1;
  SpinUs = 0;
  SpinResult = 0;
  SpillHead = SpillTail = SpillHighWater = 0;
  SpillOverflow = 0;
  for (size_t i = 0; i < kBounceCount; ++i) BounceBusy[i] = 0;
  CodecFinished = false;
  armReset();
  xQueueReset(ChunkQueue);
  xQueueReset(FreeStageQueue);
  xQueueReset(ReadyStageQueue);
  for (size_t i = 0; i < kStageCount; ++i) {
    uint8_t *slot = Stage[i];
    xQueueSend(FreeStageQueue, &slot, 0);
  }
}

static esp_err_t runCapture(uint64_t blocks, uint32_t sampleRateHz) {
  TargetBlocks = blocks;
  resetRunState();
  memset(Ring, 0xa5, kRingBytes);
  esp_cache_msync(Ring, kRingBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  StatSnapshot began = {};
  StatSnapshot ended = {};

  parlio_tx_unit_handle_t txUnit = nullptr;
  parlio_rx_unit_handle_t rxUnit = nullptr;
  parlio_rx_delimiter_handle_t delimiter = nullptr;
  esp_err_t result = createSource(&txUnit, sampleRateHz);
  if (result == ESP_OK) result = parlio_tx_unit_enable(txUnit);
  if (result == ESP_OK) {
    parlio_transmit_config_t t = {};
    t.flags.loop_transmission = true;
    result = parlio_tx_unit_transmit(txUnit, Source, kSourceBytes * 8, &t);
  }
  if (result == ESP_OK) result = createReceiver(&rxUnit, &delimiter, sampleRateHz, RequestedWidth);
  if (result == ESP_OK) result = parlio_rx_unit_enable(rxUnit, true);
  if (result == ESP_OK) {
    parlio_receive_config_t r = {};
    r.delimiter = delimiter;
    r.flags.partial_rx_en = true;
    result = parlio_rx_unit_receive(rxUnit, Ring, kRingBytes, &r);
  }
  if (result != ESP_OK) goto cleanup;

  Active = true;
  // This task is pinned to core 0, so the PARLIO RX interrupt lives there with
  // the usb task and the USB stack; the codec has core 1 to itself.
  xTaskCreatePinnedToCore(SinkOnly ? sinkTask : usbTask, "e108_usb", 4096, nullptr, 5, nullptr, 0);
  xTaskCreatePinnedToCore(harvestTask, "e108_codec", 4096, nullptr, 5, nullptr, 1);
  {
    if (Flags & kFlagLoadProbe) spinStart();
    began = snapshotStats();
    result = parlio_rx_soft_delimiter_start_stop(rxUnit, delimiter, true);
    if (result == ESP_OK) xSemaphoreTake(HarvestDone, portMAX_DELAY);
    ended = snapshotStats();
    CaptureUs = ended.wall - began.wall;
    SpinUs = CaptureUs;
    SpinResult = (Flags & kFlagLoadProbe) ? spinStop() : 0;
  }
  Idle0Us = ended.idle0 - began.idle0;
  Idle1Us = ended.idle1 - began.idle1;
  UsbdUs = ended.usbd - began.usbd;
  StatWallUs = ended.wall - began.wall;
  parlio_rx_soft_delimiter_start_stop(rxUnit, delimiter, false);
  parlio_rx_unit_disable(rxUnit);
  CodecFinished = true;
  xSemaphoreTake(UsbDone, portMAX_DELAY);
  Active = false;

cleanup:
  Active = false;
  CodecFinished = true;
  if (delimiter) parlio_del_rx_delimiter(delimiter);
  if (rxUnit) parlio_del_rx_unit(rxUnit);
  if (txUnit) {
    parlio_tx_unit_disable(txUnit);
    parlio_del_tx_unit(txUnit);
  }
  for (int pin : kPins) gpio_reset_pin(gpio_num_t(pin));
  return result;
}

// Queue a status line as its own (short) transfer and wait for its completion.
static void sendLine(int length) {
  Serial.write(StatusBuffer, length);
  Serial.flush();
  if (!Vendor.mounted()) return;
  xSemaphoreTake(StatusDone, 0);
  const Segment seg = {StatusBuffer, static_cast<uint32_t>(length), kSegStatus, 0};
  if (armPush(seg)) xSemaphoreTake(StatusDone, pdMS_TO_TICKS(kStatusWaitMs));
}

static void sendStatus(esp_err_t result) {
  const int length = snprintf(
      reinterpret_cast<char *>(StatusBuffer), kStatusBytes,
      "E109_STATUS result=%s rate_hz=%lu width=%u profile=%s sink_only=%u flags=0x%02x usb_core=%d rx_isr_core=%d "
      "stage_bytes=%lu blocks=%llu encoded=%llu sent=%llu completions=%llu direct_segments=%llu bounce_segments=%llu "
      "spill_bytes=%llu spill_high_water=%lu spill_overflow=%lu arm_failures=%lu callbacks=%llu "
      "queue_overflow=%lu fifo_overflow=%lu capture_us=%lld usb_us=%lld chunks=%lu process_us=%llu "
      "raw_sequence_bad=%lu duplicate_bad=%lu max_inflight=%lu ring_bytes=%lu sink_bytes=%llu sink_checksum=%lu "
      "stat_wall_us=%lld idle0_us=%lu idle1_us=%lu usbd_us=%lu usb_task_us=%lu codec_task_us=%lu "
      "usbd_priority=%u dma=%u dma_active=%u dwc2_arch=%u gahbcfg=0x%08lx gintmsk_rxflvl=%u codec_o2=%u codec_prefetch=%u "
      "spin_count=%lu spin_us=%lld spin_calib_per_s=%lu bench_us=%llu bench_checksum=%lu\n",
      esp_err_to_name(result), static_cast<unsigned long>(RequestedRateHz),
      RequestedWidth, WideProfile ? "wide" : "legacy", SinkOnly, Flags, UsbInitCore, RxIsrCore,
      static_cast<unsigned long>(kStageBytes),
      static_cast<unsigned long long>(TargetBlocks),
      static_cast<unsigned long long>(EncodedBlocks), static_cast<unsigned long long>(SentBytes),
      static_cast<unsigned long long>(Completions), static_cast<unsigned long long>(DirectSegments),
      static_cast<unsigned long long>(BounceSegments), static_cast<unsigned long long>(SpillBytes),
      static_cast<unsigned long>(SpillHighWater), static_cast<unsigned long>(SpillOverflow),
      static_cast<unsigned long>(ArmFailures), static_cast<unsigned long long>(CallbackBytes),
      static_cast<unsigned long>(QueueOverflow), static_cast<unsigned long>(SpillOverflow),
      static_cast<long long>(CaptureUs), static_cast<long long>(UsbUs),
      static_cast<unsigned long>(HarvestChunks), static_cast<unsigned long long>(ProcessUs),
      static_cast<unsigned long>(RawSequenceBad), static_cast<unsigned long>(DuplicateBad),
      static_cast<unsigned long>(MaxInflight), static_cast<unsigned long>(kRingBytes),
      static_cast<unsigned long long>(SinkBytes), static_cast<unsigned long>(SinkChecksum),
      static_cast<long long>(StatWallUs), static_cast<unsigned long>(Idle0Us),
      static_cast<unsigned long>(Idle1Us), static_cast<unsigned long>(UsbdUs),
      static_cast<unsigned long>(UsbTaskUs), static_cast<unsigned long>(CodecTaskUs),
      static_cast<unsigned>(UsbdPriority),
#if CFG_TUD_DWC2_DMA_ENABLE
      1U,
#else
      0U,
#endif
      dwc2DmaActive(), dwc2Arch(), static_cast<unsigned long>(dwc2Register(0x08)), dwc2RxflvlMasked(),
      static_cast<unsigned>(E108_CODEC_O2), static_cast<unsigned>(E108_CODEC_PREFETCH),
      static_cast<unsigned long>(SpinResult), static_cast<long long>(SpinUs),
      static_cast<unsigned long>(SpinCalibPerSec),
      static_cast<unsigned long long>(BenchUs), static_cast<unsigned long>(BenchChecksum));
  sendLine(length);
}

static uint64_t readLe64(const uint8_t *p) {
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i) value |= uint64_t(p[i]) << (8 * i);
  return value;
}

static uint32_t readLe32(const uint8_t *p) {
  uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i) value |= uint32_t(p[i]) << (8 * i);
  return value;
}

// USB-only probe: the 256-periodic pattern in Bounce[0] is armed over and over,
// zero-copy, with the arm ring kept full from this task.
static void runUsbProbe(uint64_t target) {
  resetRunState();
  for (size_t i = 0; i < kStageBytes; ++i) Bounce[0][i] = static_cast<uint8_t>(i);
  esp_cache_msync(Bounce[0], kStageBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  ProbeRemaining = target;
  if (Flags & kFlagLoadProbe) spinStart();
  const StatSnapshot began = snapshotStats();
  uint32_t idleTicks = 0;
  while (ProbeRemaining > 0 || armQueued() > 0) {
    while (ProbeRemaining > 0 && armQueued() < kArmDepth) {
      const uint64_t remaining = ProbeRemaining;
      const uint32_t length = static_cast<uint32_t>(remaining < kStageBytes ? remaining : kStageBytes);
      const Segment seg = {Bounce[0], length, kSegProbe, 0};
      if (!armPush(seg)) break;
      ProbeRemaining -= length;
    }
    if (xSemaphoreTake(UsbWake, pdMS_TO_TICKS(100)) == pdTRUE) {
      idleTicks = 0;
    } else if (++idleTicks >= 20 || !Vendor.mounted()) {
      break;  // host stopped reading
    }
  }
  const StatSnapshot ended = snapshotStats();
  SpinUs = ended.wall - began.wall;
  SpinResult = (Flags & kFlagLoadProbe) ? spinStop() : 0;
  const int length = snprintf(
      reinterpret_cast<char *>(StatusBuffer), kStatusBytes,
      "E109_PROBE bytes=%llu target=%llu elapsed_us=%lld completions=%llu arm_failures=%lu usb_core=%d "
      "idle0_us=%lu idle1_us=%lu usbd_us=%lu dma=%u dma_active=%u dwc2_arch=%u "
      "spin_count=%lu spin_us=%lld spin_calib_per_s=%lu flags=0x%02x\n",
      static_cast<unsigned long long>(SentBytes), static_cast<unsigned long long>(target),
      static_cast<long long>(ended.wall - began.wall), static_cast<unsigned long long>(Completions),
      static_cast<unsigned long>(ArmFailures), UsbInitCore,
      static_cast<unsigned long>(ended.idle0 - began.idle0),
      static_cast<unsigned long>(ended.idle1 - began.idle1),
      static_cast<unsigned long>(ended.usbd - began.usbd),
#if CFG_TUD_DWC2_DMA_ENABLE
      1U,
#else
      0U,
#endif
      dwc2DmaActive(), dwc2Arch(), static_cast<unsigned long>(SpinResult),
      static_cast<long long>(SpinUs), static_cast<unsigned long>(SpinCalibPerSec), Flags);
  sendLine(length);
}

static void benchmarkCodec() {
  for (size_t i = 0; i < kLegacyBlockSamples; ++i) Pending[i] = static_cast<uint16_t>(i * 257U);
  uint32_t checksum = 0;
  const int64_t began = esp_timer_get_time();
  for (size_t block = 0; block < 65536; ++block) {
    Pending[0] = static_cast<uint16_t>(block);
    encodeBlock(Pending, BenchWire);
    checksum += BenchWire[block % kLegacyWireBlockBytes];
  }
  BenchUs = esp_timer_get_time() - began;
  BenchChecksum = checksum;
}

static void captureControlTask(void *) {
  if (CommandMode == 'P') {
    runUsbProbe(TargetBlocks);
  } else {
    const esp_err_t result = runCapture(TargetBlocks, RequestedRateHz);
    sendStatus(result);
  }
  RunPending = false;
  vTaskDelete(nullptr);
}

static void usbBeginTask(void *) {
  UsbInitCore = xPortGetCoreID();
  UsbReady = Device.begin(UsbConfig);
  xSemaphoreGive(UsbBeginDone);
  vTaskDelete(nullptr);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Ring = static_cast<uint8_t *>(heap_caps_aligned_alloc(
      64, kRingBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  Source = static_cast<uint8_t *>(heap_caps_aligned_alloc(
      64, kSourceBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  Spill = static_cast<uint8_t *>(heap_caps_malloc(
      kSpillBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  ChunkQueue = xQueueCreate(kQueueDepth, sizeof(Chunk));
  FreeStageQueue = xQueueCreate(kStageCount, sizeof(uint8_t *));
  ReadyStageQueue = xQueueCreate(kStageCount, sizeof(WireChunk));
  HarvestDone = xSemaphoreCreateBinary();
  UsbDone = xSemaphoreCreateBinary();
  UsbWake = xSemaphoreCreateBinary();
  StatusDone = xSemaphoreCreateBinary();
  UsbBeginDone = xSemaphoreCreateBinary();
  SpinDone = xSemaphoreCreateBinary();
  for (size_t i = 0; i < kSourceBytes; ++i) {
    const uint8_t value = static_cast<uint8_t>(i);
    Source[i] = value ^ (value >> 1);
  }
  esp_cache_msync(Source, kSourceBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  benchmarkCodec();

  UsbConfig.vid = kVid;
  UsbConfig.pid = kPid;
  UsbConfig.manufacturer = "wch-protocols";
  UsbConfig.product = "E109 P4 stream soak";
  // Same serial as E104..E107 so the Windows devnode / WinUSB binding persists.
  UsbConfig.serialNumber = "e104-p4-windows-v1";
  UsbConfig.controller = EspUsbController::HighSpeed;
  UsbConfig.webusbEnabled = true;
  xTaskCreatePinnedToCore(usbBeginTask, "e108_usb_begin", 8192, nullptr, 6, nullptr, kUsbCore);
  xSemaphoreTake(UsbBeginDone, portMAX_DELAY);
  UsbdTask = xTaskGetHandle("espusb-device");
  UsbdPriority = UsbdTask ? uxTaskPriorityGet(UsbdTask) : 0;
  // Spin calibration: 300 ms with only the USB stack and the tick running on core 0.
  spinStart();
  delay(300);
  SpinCalibPerSec = static_cast<uint32_t>(static_cast<uint64_t>(spinStop()) * 1000U / 300U);
  Serial.printf("E109_READY usb=%u usb_core=%d usbd_priority=%u dma=%u dma_active=%u dwc2_arch=%u gahbcfg=0x%08lx gintmsk_rxflvl=%u codec_o2=%u "
                "spin_calib_per_s=%lu stage_bytes=%u stages=%u "
                "free_internal=%u free_spiram=%u buffers=%u bench_us=%llu checksum=%lu\n",
                UsbReady, UsbInitCore, static_cast<unsigned>(UsbdPriority),
#if CFG_TUD_DWC2_DMA_ENABLE
                1U,
#else
                0U,
#endif
                dwc2DmaActive(), dwc2Arch(), static_cast<unsigned long>(dwc2Register(0x08)), dwc2RxflvlMasked(),
                static_cast<unsigned>(E108_CODEC_O2),
                static_cast<unsigned long>(SpinCalibPerSec),
                static_cast<unsigned>(kStageBytes), static_cast<unsigned>(kStageCount),
                static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                Ring && Source && Spill && ChunkQueue,
                static_cast<unsigned long long>(BenchUs), static_cast<unsigned long>(BenchChecksum));
}

void loop() {
  if (RunPending || !CommandPending) {
    delay(1);
    return;
  }
  uint8_t command[kCommandBytes];
  memcpy(command, CommandMailbox, kCommandBytes);
  CommandPending = false;
  if ((command[0] != 'E' && command[0] != 'I') ||
      (command[1] != '6' && command[1] != '8' && command[1] != 'W' && command[1] != 'P')) return;
  const uint32_t rateHz = readLe32(command + 2);
  const uint64_t blocks = readLe64(command + 6);
  if (blocks == 0) return;
  TargetBlocks = blocks;
  RequestedRateHz = command[1] == 'P' ? kSampleRateHz : rateHz;
  RequestedWidth = command[1] == '8' ? 8 : 16;
  WideProfile = command[1] == 'W';
  SinkOnly = command[0] == 'I';
  Flags = command[14];
  if (command[1] != 'P' && (RequestedRateHz < 1000000 || RequestedRateHz > 160000000)) return;
  CommandMode = command[1] == 'P' ? 'P' : static_cast<char>(command[0]);
  RunPending = true;
  xTaskCreatePinnedToCore(captureControlTask, "e108_ctl", 4096, nullptr, 6, nullptr, 0);
}
