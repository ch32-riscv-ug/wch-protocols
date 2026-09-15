// E112: E111 (dual-core zero-copy stream) plus two 16-channel allocation
// profiles: F = 4 full + 12 D32 (128 samples -> 70 bytes), V = 5 full + 11 D32
// (128 samples -> 86 bytes).
//
// PARLIO chunks (3,840 bytes = a whole number of codec blocks) carry a sequence
// number from the ISR. Two codec workers (core 0 and core 1) take chunks from
// one queue and encode each chunk straight into its final place: the wire
// offset is seq * chunkWire, which selects the stage buffer and the offset in
// it, so no ordering step is needed on the codec side. A stage is published by
// whichever worker completes its byte count; the usb task sends stages in
// index order and releases a buffer when its transfer completes. Everything
// else (zero-copy arm ring, PSRAM spill, probe, stats) is E108 / E110.
//
// Requires EspUsbDevice 2.3.0 with e108's espusbdevice-e108.patch and e110's
// dcd-dwc2-in-fifo-packets.patch (/tmp/EspUsbDevice-e110).

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
// The PARLIO driver mounts the ring as DMA nodes of up to 4,032 bytes (the last
// two split evenly), so chunk lengths are not a whole number of blocks for the
// 16-bit profiles. Placement therefore uses the raw stream offset carried by
// each chunk: the worker that receives a chunk completes the block that
// straddles its head from the previous chunk's tail (still in the ring), then
// encodes its whole blocks, and leaves its own partial tail to the next chunk.
constexpr size_t kRingBytes = 65280;
constexpr size_t kDelimiterBytes = 65280;
constexpr size_t kWorkers = 2;
constexpr uint32_t kStageWaitTimeoutUs = 500000;
constexpr size_t kQueueDepth = 128;
constexpr size_t kSourceBytes = 8192;
constexpr uint32_t kSourceDivider = 4;
// One stage is one USB transfer. 27,136 = lcm(53, 512): the wide profile fills
// 512 blocks, the 25-byte profiles fill 1,024 blocks (25,600 B). Both are
// multiples of 512 (whole packets) and of 64 (cache lines).
constexpr size_t kStageBytes = 27136;
constexpr size_t kStageFillWide = 512 * kWideWireBlockBytes;
constexpr size_t kStageFillLegacy = 1024 * kLegacyWireBlockBytes;
// 16-channel allocation profiles (E112): 4 full + 12 D32 and 5 full + 11 D32.
constexpr size_t kFourWireBlockBytes = 70;   // 4 x 128 bit + 12 lanes x 4 snapshots
constexpr size_t kFiveWireBlockBytes = 86;   // 5 x 128 bit (80 B) + 11 lanes x 4 snapshots (44 bit -> 6 B)
constexpr size_t kStageFillFour = 256 * kFourWireBlockBytes;   // 17,920 = 35 x 512
constexpr size_t kStageFillFive = 256 * kFiveWireBlockBytes;   // 22,016 = 43 x 512
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
constexpr uint8_t kFlagSingleCore = 0x10; // one codec worker on core 1 (E108 layout) for comparison
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
  uint32_t length;
  uint32_t seq;
  uint64_t rawOffset;  // stream position of data[0]; blocks are placed from this, not from seq
};

struct WireChunk {
  uint8_t *data;
  size_t length;
  uint32_t index;  // stage index; the usb task sends in this order
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
QueueHandle_t ReadyStageQueue;
// Stage bookkeeping shared by the workers and the usb task. Buffer b hosts
// stage indices b, b+N, b+2N, ...; StageFreed[b] counts the rounds released.
volatile uint32_t StageFilled[kStageCount];
volatile uint32_t StageFreed[kStageCount];
portMUX_TYPE StageMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t StageWaits;
volatile uint32_t StageWaitTimeouts;
volatile uint32_t ShortChunks;
volatile uint32_t ChunkSeq;
volatile uint32_t ChunkMin;
volatile uint32_t ChunkMax;
volatile bool RunAborted;
volatile uint64_t ProcessedRaw;
volatile uint32_t WorkerChunks[kWorkers];
volatile uint32_t WorkerUs[kWorkers];
volatile uint32_t WorkerCount;
size_t StageFill;
size_t WireBlockBytes;
uint64_t TotalWireBytes;
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
char ProfileCode = '6';  // '6' legacy 16-bit, '8' eight, 'W' wide, 'F' four+12 D32, 'V' five+11 D32
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
  xTaskCreatePinnedToCore(spinTask, "e111_spin", 2048, nullptr, 1, nullptr, 0);
}

static uint32_t spinStop() {
  SpinRun = false;
  xSemaphoreTake(SpinDone, pdMS_TO_TICKS(200));
  return SpinCount;
}

static size_t activeWireBlockBytes() {
  switch (ProfileCode) {
    case 'W': return kWideWireBlockBytes;
    case 'F': return kFourWireBlockBytes;
    case 'V': return kFiveWireBlockBytes;
    default: return kLegacyWireBlockBytes;
  }
}

static size_t activeStageFill() {
  switch (ProfileCode) {
    case 'W': return kStageFillWide;
    case 'F': return kStageFillFour;
    case 'V': return kStageFillFive;
    default: return kStageFillLegacy;
  }
}

static const char *activeProfileName() {
  switch (ProfileCode) {
    case 'W': return "wide";
    case 'F': return "four";
    case 'V': return "five";
    default: return "legacy";
  }
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

// Four 16-bit samples (two words) -> their low 4 bits, packed into 16 bits.
static inline __attribute__((always_inline)) uint32_t gatherLow4x4Halves(uint32_t w0, uint32_t w1) {
  const uint32_t x = (w0 & 0x000f000fU) | ((w1 & 0x000f000fU) << 8);
  return (x | (x >> 12)) & 0xffffU;
}

// Four 16-bit samples (two words) -> their low 5 bits, packed into 20 bits.
static inline __attribute__((always_inline)) uint32_t gatherLow5x4Halves(uint32_t w0, uint32_t w1) {
  return (w0 & 0x1fU) | ((w0 >> 11) & 0x3e0U) | ((w1 & 0x1fU) << 10) | ((w1 >> 1) & 0xf8000U);
}

static inline __attribute__((always_inline)) void store32(uint8_t *out, uint32_t v) {
  out[0] = static_cast<uint8_t>(v);
  out[1] = static_cast<uint8_t>(v >> 8);
  out[2] = static_cast<uint8_t>(v >> 16);
  out[3] = static_cast<uint8_t>(v >> 24);
}

// E112 F: 4 full + 12 D32, 128 samples -> 70 bytes. Snapshots at samples 0/32/64/96.
static inline __attribute__((always_inline)) void encodeBlockFour(
    const uint16_t *samples, uint8_t *wire) {
  const auto *words = reinterpret_cast<const uint32_t *>(samples);
  for (size_t group = 0; group < 16; ++group) {
    const uint32_t *w = words + group * 4;
    store32(wire + group * 4, gatherLow4x4Halves(w[0], w[1]) | (gatherLow4x4Halves(w[2], w[3]) << 16));
  }
  const uint64_t d = static_cast<uint64_t>((words[0] & 0xffffU) >> 4) |
      (static_cast<uint64_t>((words[16] & 0xffffU) >> 4) << 12) |
      (static_cast<uint64_t>((words[32] & 0xffffU) >> 4) << 24) |
      (static_cast<uint64_t>((words[48] & 0xffffU) >> 4) << 36);
  for (size_t i = 0; i < 6; ++i) wire[64 + i] = static_cast<uint8_t>(d >> (8 * i));
}

// E112 V: 5 full + 11 D32, 128 samples -> 86 bytes.
static inline __attribute__((always_inline)) void encodeBlockFive(
    const uint16_t *samples, uint8_t *wire) {
  const auto *words = reinterpret_cast<const uint32_t *>(samples);
  for (size_t group = 0; group < 16; ++group) {
    const uint32_t *w = words + group * 4;
    const uint32_t r0 = gatherLow5x4Halves(w[0], w[1]);
    const uint32_t r1 = gatherLow5x4Halves(w[2], w[3]);
    uint8_t *out = wire + group * 5;
    out[0] = static_cast<uint8_t>(r0);
    out[1] = static_cast<uint8_t>(r0 >> 8);
    out[2] = static_cast<uint8_t>((r0 >> 16) | (r1 << 4));
    out[3] = static_cast<uint8_t>(r1 >> 4);
    out[4] = static_cast<uint8_t>(r1 >> 12);
  }
  const uint64_t d = static_cast<uint64_t>((words[0] & 0xffffU) >> 5) |
      (static_cast<uint64_t>((words[16] & 0xffffU) >> 5) << 11) |
      (static_cast<uint64_t>((words[32] & 0xffffU) >> 5) << 22) |
      (static_cast<uint64_t>((words[48] & 0xffffU) >> 5) << 33);
  for (size_t i = 0; i < 6; ++i) wire[80 + i] = static_cast<uint8_t>(d >> (8 * i));
}

enum class Profile { Legacy16, Eight, Wide, FourD32, FiveD32 };

static uint64_t stageExpectedFill(uint32_t index) {
  const uint64_t start = static_cast<uint64_t>(index) * StageFill;
  if (start >= TotalWireBytes) return 0;
  const uint64_t remain = TotalWireBytes - start;
  return remain < StageFill ? remain : StageFill;
}

// Block until buffer (index % N) has been released for round index / N. A
// bounded wait: on timeout the run is aborted instead of starving IDLE0 into
// the task watchdog.
static inline bool waitStage(uint32_t index) {
  const size_t b = index % kStageCount;
  const uint32_t round = index / kStageCount;
  if (StageFreed[b] >= round) return true;
  ++StageWaits;
  const int64_t deadline = esp_timer_get_time() + kStageWaitTimeoutUs;
  while (StageFreed[b] < round) {
    if (RunAborted) return false;
    if (esp_timer_get_time() > deadline) {
      ++StageWaitTimeouts;
      RunAborted = true;
      return false;
    }
    taskYIELD();
  }
  return true;
}

static inline void writebackLines(const uint8_t *begin, size_t bytes) {
  const uintptr_t start = reinterpret_cast<uintptr_t>(begin) & ~uintptr_t(63);
  const uintptr_t end = (reinterpret_cast<uintptr_t>(begin) + bytes + 63) & ~uintptr_t(63);
  esp_cache_msync(reinterpret_cast<void *>(start), end - start, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

// Account bytes written into stage `index`; the worker that completes the
// stage publishes it.
static void commitStage(uint32_t index, size_t bytes) {
  const size_t b = index % kStageCount;
  bool complete = false;
  uint32_t filled = 0;
  portENTER_CRITICAL(&StageMux);
  StageFilled[b] += bytes;
  filled = StageFilled[b];
  if (filled >= stageExpectedFill(index)) {
    complete = true;
    StageFilled[b] = 0;
  }
  portEXIT_CRITICAL(&StageMux);
  if (complete) {
    const WireChunk ready = {Stage[b], filled, index};
    xQueueSend(ReadyStageQueue, &ready, portMAX_DELAY);
  }
}

template <Profile P, bool kCheck>
static void codecWorkerBody(unsigned worker) {
  constexpr bool kWideBlock = P == Profile::Wide || P == Profile::FourD32 || P == Profile::FiveD32;
  constexpr size_t kSamples = kWideBlock ? kWideBlockSamples : kLegacyBlockSamples;
  constexpr size_t kRaw = kSamples * (P == Profile::Eight ? 1 : 2);
  constexpr size_t kWire = P == Profile::Wide ? kWideWireBlockBytes
      : P == Profile::FourD32 ? kFourWireBlockBytes
      : P == Profile::FiveD32 ? kFiveWireBlockBytes : kLegacyWireBlockBytes;
  constexpr uint8_t kExpectedDelta = kSamples / 4;
  constexpr uint8_t kDeltaMargin = kSamples / 64;
  const uint64_t target = TargetBlocks;
  const size_t fill = StageFill;
  uint32_t idle = 0;
  uint32_t chunks = 0;
  uint32_t rawSequenceBad = 0;
  uint32_t duplicateBad = 0;
  size_t maxInflight = 0;
  uint8_t straddle[kRaw] __attribute__((aligned(64)));
  const auto encode = [](const uint8_t *in, uint8_t *out) __attribute__((always_inline)) {
    if constexpr (P == Profile::Wide) encodeBlockWide(reinterpret_cast<const uint16_t *>(in), out);
    else if constexpr (P == Profile::FourD32) encodeBlockFour(reinterpret_cast<const uint16_t *>(in), out);
    else if constexpr (P == Profile::FiveD32) encodeBlockFive(reinterpret_cast<const uint16_t *>(in), out);
    else if constexpr (P == Profile::Eight) encodeBlock8(in, out);
    else encodeBlock(reinterpret_cast<const uint16_t *>(in), out);
  };
  // Gray check on consecutive blocks handled by this worker inside one chunk.
  bool previousValid = false;
  uint8_t previousBinary = 0;
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
  // Encode `count` consecutive blocks starting at block index `first` from `in`
  // into their stage(s). Returns false when the run was aborted.
  const auto place = [&](uint64_t first, const uint8_t *in, size_t count) -> bool {
    const uint64_t wireOffset = first * kWire;
    uint32_t stage = static_cast<uint32_t>(wireOffset / fill);
    size_t offset = static_cast<size_t>(wireOffset % fill);
    while (count) {
      size_t n = (fill - offset) / kWire;
      if (n > count) n = count;
      if (!waitStage(stage)) return false;
      uint8_t *out = Stage[stage % kStageCount] + offset;
      uint8_t *const outBegin = out;
      for (size_t i = 0; i < n; ++i, in += kRaw, out += kWire) {
#if E108_CODEC_PREFETCH
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
      writebackLines(outBegin, n * kWire);
      commitStage(stage, n * kWire);
      count -= n;
      ++stage;
      offset = 0;
    }
    return true;
  };
  while (!RunAborted) {
    Chunk chunk = {};
    if (xQueueReceive(ChunkQueue, &chunk, pdMS_TO_TICKS(200)) != pdTRUE) {
      if (++idle >= 10) break;
      continue;
    }
    idle = 0;
    previousValid = false;
    const size_t tail = static_cast<size_t>(chunk.rawOffset % kRaw);  // bytes of a block begun in the previous chunk
    uint64_t block = chunk.rawOffset / kRaw;                             // index of the block containing data[0]
    size_t at = 0;
    if (block >= target) break;
    if (tail != 0) {
      const size_t lead = kRaw - tail;
      if (chunk.length < lead) {
        ++ShortChunks;
        continue;
      }
      // The previous chunk's tail is still in the ring just before this chunk
      // (or at the ring end when this chunk starts the ring).
      const uint8_t *previous = chunk.data == Ring ? Ring + kRingBytes - tail : chunk.data - tail;
      memcpy(straddle, previous, tail);
      memcpy(straddle + tail, chunk.data, lead);
      if (!place(block, straddle, 1)) break;
      ++block;
      at = lead;
    }
    size_t full = (chunk.length - at) / kRaw;
    if (block + full > target) full = static_cast<size_t>(target - block);
    if (full && !place(block, chunk.data + at, full)) break;
    ++chunks;
    __atomic_fetch_add(&EncodedBlocks, static_cast<uint64_t>(full) + (tail ? 1 : 0), __ATOMIC_RELAXED);
    const uint64_t processed = __atomic_add_fetch(&ProcessedRaw, static_cast<uint64_t>(chunk.length), __ATOMIC_RELAXED);
    const uint64_t callback = CallbackBytes;
    const size_t inflight = callback > processed ? static_cast<size_t>(callback - processed) : 0;
    if (inflight > maxInflight) maxInflight = inflight;
  }
  WorkerChunks[worker] = chunks;
  WorkerUs[worker] = taskRunTime(nullptr);
  __atomic_fetch_add(&RawSequenceBad, rawSequenceBad, __ATOMIC_RELAXED);
  __atomic_fetch_add(&DuplicateBad, duplicateBad, __ATOMIC_RELAXED);
  portENTER_CRITICAL(&StageMux);
  if (maxInflight > MaxInflight) MaxInflight = maxInflight;
  portEXIT_CRITICAL(&StageMux);
}

static void codecWorkerTask(void *arg) {
  const unsigned worker = static_cast<unsigned>(reinterpret_cast<uintptr_t>(arg));
  const bool check = (Flags & kFlagNoCheck) == 0;
  switch (ProfileCode) {
    case 'W': check ? codecWorkerBody<Profile::Wide, true>(worker) : codecWorkerBody<Profile::Wide, false>(worker); break;
    case 'F': check ? codecWorkerBody<Profile::FourD32, true>(worker) : codecWorkerBody<Profile::FourD32, false>(worker); break;
    case 'V': check ? codecWorkerBody<Profile::FiveD32, true>(worker) : codecWorkerBody<Profile::FiveD32, false>(worker); break;
    case '8': check ? codecWorkerBody<Profile::Eight, true>(worker) : codecWorkerBody<Profile::Eight, false>(worker); break;
    default: check ? codecWorkerBody<Profile::Legacy16, true>(worker) : codecWorkerBody<Profile::Legacy16, false>(worker); break;
  }
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

static void releaseStage(uint8_t slot) {
  if (slot < kStageCount) ++StageFreed[slot];
}

static void dispatchStage(const WireChunk &ready) {
  const bool direct = (Flags & kFlagNoDirect) == 0 && spillUsed() == 0 && armQueued() < kDirectDepth;
  if (direct) {
    const Segment seg = {ready.data, static_cast<uint32_t>(ready.length), kSegStage, stageSlot(ready.data)};
    armPush(seg);
    ++DirectSegments;
  } else {
    spillPush(ready.data, ready.length);
    releaseStage(stageSlot(ready.data));
  }
}

static void usbTask(void *) {
  const int64_t began = esp_timer_get_time();
  WireChunk held[kStageCount] = {};
  bool heldValid[kStageCount] = {};
  uint32_t nextSend = 0;
  while (true) {
    WireChunk ready = {};
    if (xQueueReceive(ReadyStageQueue, &ready, pdMS_TO_TICKS(1)) == pdTRUE) {
      if (ready.index == nextSend) {
        dispatchStage(ready);
        ++nextSend;
        while (heldValid[nextSend % kStageCount] && held[nextSend % kStageCount].index == nextSend) {
          heldValid[nextSend % kStageCount] = false;
          dispatchStage(held[nextSend % kStageCount]);
          ++nextSend;
        }
      } else {
        held[ready.index % kStageCount] = ready;
        heldValid[ready.index % kStageCount] = true;
      }
    }
    const bool drained = CodecFinished && uxQueueMessagesWaiting(ReadyStageQueue) == 0;
    refillBounces(drained);
    if (RunAborted && CodecFinished) break;
    bool anyHeld = false;
    for (size_t i = 0; i < kStageCount; ++i) anyHeld |= heldValid[i];
    if (drained && !anyHeld && spillUsed() == 0 && armQueued() == 0) break;
  }
  UsbUs = esp_timer_get_time() - began;
  UsbTaskUs = taskRunTime(nullptr);
  xSemaphoreGive(UsbDone);
  vTaskDelete(nullptr);
}

// Internal-sink mode: release stages without sending, so the codec alone is measured.
static void sinkTask(void *) {
  while (true) {
    WireChunk ready = {};
    if (xQueueReceive(ReadyStageQueue, &ready, pdMS_TO_TICKS(1)) == pdTRUE) {
      uint32_t checksum = SinkChecksum;
      for (size_t i = 0; i < ready.length; i += 64) checksum += ready.data[i];
      SinkChecksum = checksum;
      SinkBytes += ready.length;
      releaseStage(stageSlot(ready.data));
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
    case kSegStage:
      releaseStage(seg.slot);
      break;
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
  const Chunk chunk = {static_cast<const uint8_t *>(event->data),
                       static_cast<uint32_t>(event->recv_bytes), ChunkSeq++, CallbackBytes};
  CallbackBytes += event->recv_bytes;
  if (event->recv_bytes < ChunkMin) ChunkMin = event->recv_bytes;
  if (event->recv_bytes > ChunkMax) ChunkMax = event->recv_bytes;
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
  UsbTaskUs = 0;
  for (size_t i = 0; i < kWorkers; ++i) WorkerChunks[i] = WorkerUs[i] = 0;
  for (size_t i = 0; i < kStageCount; ++i) StageFilled[i] = StageFreed[i] = 0;
  StageWaits = StageWaitTimeouts = ShortChunks = ChunkSeq = 0;
  ChunkMin = 0xffffffffU;
  ChunkMax = 0;
  RunAborted = false;
  ProcessedRaw = 0;
  WireBlockBytes = activeWireBlockBytes();
  StageFill = activeStageFill();
  TotalWireBytes = TargetBlocks * WireBlockBytes;
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
  xQueueReset(ReadyStageQueue);
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
  // usb / sink task above the workers on core 0 so a worker there never delays
  // an arm; the usbd task (priority 24) is above both.
  xTaskCreatePinnedToCore(SinkOnly ? sinkTask : usbTask, "e111_usb", 4096, nullptr, 6, nullptr, 0);
  WorkerCount = (Flags & kFlagSingleCore) ? 1 : kWorkers;
  xTaskCreatePinnedToCore(codecWorkerTask, "e111_codec1", 4096, reinterpret_cast<void *>(1), 5, nullptr, 1);
  if (WorkerCount == 2) xTaskCreatePinnedToCore(codecWorkerTask, "e111_codec0", 4096, reinterpret_cast<void *>(0), 5, nullptr, 0);
  {
    if (Flags & kFlagLoadProbe) spinStart();
    began = snapshotStats();
    result = parlio_rx_soft_delimiter_start_stop(rxUnit, delimiter, true);
    if (result == ESP_OK) {
      for (uint32_t w = 0; w < WorkerCount; ++w) xSemaphoreTake(HarvestDone, portMAX_DELAY);
    }
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
      "E112_STATUS result=%s rate_hz=%lu width=%u profile=%s sink_only=%u flags=0x%02x usb_core=%d rx_isr_core=%d "
      "stage_bytes=%lu blocks=%llu encoded=%llu sent=%llu completions=%llu direct_segments=%llu bounce_segments=%llu "
      "spill_bytes=%llu spill_high_water=%lu spill_overflow=%lu arm_failures=%lu callbacks=%llu "
      "queue_overflow=%lu fifo_overflow=%lu capture_us=%lld usb_us=%lld chunks=%lu process_us=%llu "
      "raw_sequence_bad=%lu duplicate_bad=%lu max_inflight=%lu ring_bytes=%lu sink_bytes=%llu sink_checksum=%lu "
      "stat_wall_us=%lld idle0_us=%lu idle1_us=%lu usbd_us=%lu usb_task_us=%lu codec0_task_us=%lu codec1_task_us=%lu "
      "workers=%lu chunks0=%lu chunks1=%lu chunk_min=%lu chunk_max=%lu short_chunks=%lu stage_waits=%lu stage_wait_timeouts=%lu aborted=%u "
      "usbd_priority=%u dma=%u dma_active=%u dwc2_arch=%u gahbcfg=0x%08lx gintmsk_rxflvl=%u codec_o2=%u codec_prefetch=%u "
      "spin_count=%lu spin_us=%lld spin_calib_per_s=%lu bench_us=%llu bench_checksum=%lu\n",
      esp_err_to_name(result), static_cast<unsigned long>(RequestedRateHz),
      RequestedWidth, activeProfileName(), SinkOnly, Flags, UsbInitCore, RxIsrCore,
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
      static_cast<unsigned long>(UsbTaskUs), static_cast<unsigned long>(WorkerUs[0]), static_cast<unsigned long>(WorkerUs[1]),
      static_cast<unsigned long>(WorkerCount), static_cast<unsigned long>(WorkerChunks[0]), static_cast<unsigned long>(WorkerChunks[1]),
      static_cast<unsigned long>(ChunkMin == 0xffffffffU ? 0 : ChunkMin), static_cast<unsigned long>(ChunkMax),
      static_cast<unsigned long>(ShortChunks), static_cast<unsigned long>(StageWaits),
      static_cast<unsigned long>(StageWaitTimeouts), RunAborted ? 1U : 0U,
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
      "E112_PROBE bytes=%llu target=%llu elapsed_us=%lld completions=%llu arm_failures=%lu usb_core=%d "
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
  ReadyStageQueue = xQueueCreate(kStageCount, sizeof(WireChunk));
  HarvestDone = xSemaphoreCreateCounting(kWorkers, 0);
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
  UsbConfig.product = "E112 P4 16ch allocation profiles";
  // Same serial as E104..E107 so the Windows devnode / WinUSB binding persists.
  UsbConfig.serialNumber = "e104-p4-windows-v1";
  UsbConfig.controller = EspUsbController::HighSpeed;
  UsbConfig.webusbEnabled = true;
  xTaskCreatePinnedToCore(usbBeginTask, "e111_usb_begin", 8192, nullptr, 6, nullptr, kUsbCore);
  xSemaphoreTake(UsbBeginDone, portMAX_DELAY);
  UsbdTask = xTaskGetHandle("espusb-device");
  UsbdPriority = UsbdTask ? uxTaskPriorityGet(UsbdTask) : 0;
  // Spin calibration: 300 ms with only the USB stack and the tick running on core 0.
  spinStart();
  delay(300);
  SpinCalibPerSec = static_cast<uint32_t>(static_cast<uint64_t>(spinStop()) * 1000U / 300U);
  Serial.printf("E112_READY usb=%u usb_core=%d usbd_priority=%u dma=%u dma_active=%u dwc2_arch=%u gahbcfg=0x%08lx gintmsk_rxflvl=%u codec_o2=%u "
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
      (command[1] != '6' && command[1] != '8' && command[1] != 'W' && command[1] != 'F' && command[1] != 'V' &&
       command[1] != 'P')) return;
  const uint32_t rateHz = readLe32(command + 2);
  const uint64_t blocks = readLe64(command + 6);
  if (blocks == 0) return;
  TargetBlocks = blocks;
  RequestedRateHz = command[1] == 'P' ? kSampleRateHz : rateHz;
  RequestedWidth = command[1] == '8' ? 8 : 16;
  WideProfile = command[1] == 'W';
  ProfileCode = command[1] == 'P' ? ProfileCode : static_cast<char>(command[1]);
  SinkOnly = command[0] == 'I';
  Flags = command[14];
  if (command[1] != 'P' && (RequestedRateHz < 1000000 || RequestedRateHz > 160000000)) return;
  CommandMode = command[1] == 'P' ? 'P' : static_cast<char>(command[0]);
  RunPending = true;
  xTaskCreatePinnedToCore(captureControlTask, "e111_ctl", 4096, nullptr, 6, nullptr, 0);
}
