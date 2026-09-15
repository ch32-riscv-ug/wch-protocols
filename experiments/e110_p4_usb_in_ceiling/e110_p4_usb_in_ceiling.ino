// E110: USB HS bulk IN ceiling, measured from scratch. No capture, no codec:
// a 64 KiB 256-periodic pattern is streamed zero-copy from internal RAM with
// E108's arm ring. The command selects the device transfer length, the arm ring
// depth, and whether the next transfer is armed inside the TX-complete callback
// (chain) or from the task. Device-side timing of every transfer isolates the
// re-arm gap from the on-the-wire time.
//
// Requires EspUsbDevice 2.3.0 with e108's espusbdevice-e108.patch
// (non-buffered vendor class, direct RX / TX-complete hooks, zero-copy write).

#include <Arduino.h>
#include "EspUsbDevice.h"

#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace {

constexpr size_t kPatternBytes = 65536;
constexpr uint32_t kMaxTransfer = 65024;  // 127 packets: usbd_edpt_xfer takes uint16_t
constexpr size_t kArmDepthMax = 4;
constexpr size_t kStatusBytes = 1024;
constexpr size_t kCommandBytes = 16;
constexpr uint32_t kStatusWaitMs = 2000;
constexpr uint16_t kVid = 0x303a;
constexpr uint16_t kPid = 0x4021;
constexpr int kUsbCore = 0;
constexpr uintptr_t kDwc2HsBase = 0x50000000UL;

constexpr uint8_t kFlagDepthMask = 0x03;   // arm ring depth - 1
constexpr uint8_t kFlagLoadProbe = 0x04;   // priority-1 spin task on core 0
constexpr uint8_t kFlagTaskRearm = 0x08;   // do not chain from the callback; the task re-arms

enum SegmentKind : uint8_t { kSegProbe, kSegStatus };

struct Segment {
  const uint8_t *data;
  uint32_t length;
  uint8_t kind;
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
SemaphoreHandle_t UsbWake;
SemaphoreHandle_t StatusDone;
SemaphoreHandle_t UsbBeginDone;
SemaphoreHandle_t SpinDone;
uint8_t Pattern[kPatternBytes] __attribute__((aligned(64)));
uint8_t StatusBuffer[kStatusBytes] __attribute__((aligned(64)));

Segment ArmRing[kArmDepthMax];
volatile uint32_t ArmHead;
volatile uint32_t ArmTail;
volatile bool Inflight;
Segment InflightSegment;
portMUX_TYPE ArmMux = portMUX_INITIALIZER_UNLOCKED;

volatile uint64_t SentBytes;
volatile uint64_t Completions;
volatile uint32_t ArmFailures;
volatile int64_t ArmedAt;
volatile int64_t CompletedAt;
volatile int64_t FirstArmAt;
volatile int64_t LastCompleteAt;
volatile uint64_t DurSumUs;
volatile uint32_t DurMinUs;
volatile uint32_t DurMaxUs;
volatile uint64_t GapSumUs;
volatile uint32_t GapMaxUs;
volatile uint32_t GapCount;
volatile bool ChainInCallback;
volatile uint64_t ProbeRemaining;
uint32_t TransferBytes;
uint32_t ArmDepth;
uint8_t Flags;
volatile bool RunPending;
volatile bool CommandPending;
uint8_t CommandMailbox[kCommandBytes];
int UsbInitCore = -1;
TaskHandle_t UsbdTask;
UBaseType_t UsbdPriority;
bool UsbReady;
volatile bool SpinRun;
volatile uint32_t SpinCount;
uint32_t SpinCalibPerSec;

static uint32_t dwc2Register(uint32_t offset) {
  return *reinterpret_cast<volatile uint32_t *>(kDwc2HsBase + offset);
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
  xTaskCreatePinnedToCore(spinTask, "e110_spin", 2048, nullptr, 1, nullptr, 0);
}

static uint32_t spinStop() {
  SpinRun = false;
  xSemaphoreTake(SpinDone, pdMS_TO_TICKS(200));
  return SpinCount;
}

static uint32_t armQueued() {
  portENTER_CRITICAL(&ArmMux);
  const uint32_t queued = ArmTail - ArmHead + (Inflight ? 1U : 0U);
  portEXIT_CRITICAL(&ArmMux);
  return queued;
}

static void tryArm() {
  Segment seg = {};
  bool claimed = false;
  portENTER_CRITICAL(&ArmMux);
  if (!Inflight && ArmHead != ArmTail) {
    seg = ArmRing[ArmHead % kArmDepthMax];
    ++ArmHead;
    Inflight = true;
    InflightSegment = seg;
    claimed = true;
  }
  portEXIT_CRITICAL(&ArmMux);
  if (!claimed) return;
  const int64_t now = esp_timer_get_time();
  if (seg.kind == kSegProbe) {
    if (FirstArmAt == 0) FirstArmAt = now;
    if (CompletedAt != 0) {
      const uint32_t gap = static_cast<uint32_t>(now - CompletedAt);
      GapSumUs += gap;
      if (gap > GapMaxUs) GapMaxUs = gap;
      ++GapCount;
      CompletedAt = 0;
    }
  }
  ArmedAt = now;
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
  if (ArmTail - ArmHead < kArmDepthMax) {
    ArmRing[ArmTail % kArmDepthMax] = seg;
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

}  // namespace

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
  const int64_t now = esp_timer_get_time();
  SentBytes += sentBytes;
  ++Completions;
  if (seg.kind == kSegProbe) {
    const uint32_t dur = static_cast<uint32_t>(now - ArmedAt);
    DurSumUs += dur;
    if (dur < DurMinUs) DurMinUs = dur;
    if (dur > DurMaxUs) DurMaxUs = dur;
    CompletedAt = now;
    LastCompleteAt = now;
    if (ChainInCallback) tryArm();
  } else if (seg.kind == kSegStatus) {
    xSemaphoreGive(StatusDone);
  }
  if (UsbWake) xSemaphoreGive(UsbWake);
}

extern "C" void esp_usb_device_direct_vendor_rx(const uint8_t *buffer, uint32_t length) {
  if (length < kCommandBytes || CommandPending) return;
  memcpy(CommandMailbox, buffer, kCommandBytes);
  CommandPending = true;
}

namespace {

static void sendLine(int length) {
  Serial.write(StatusBuffer, length);
  Serial.flush();
  if (!Vendor.mounted()) return;
  xSemaphoreTake(StatusDone, 0);
  const Segment seg = {StatusBuffer, static_cast<uint32_t>(length), kSegStatus};
  if (armPush(seg)) xSemaphoreTake(StatusDone, pdMS_TO_TICKS(kStatusWaitMs));
}

static void runProbe(uint64_t target) {
  SentBytes = Completions = 0;
  ArmFailures = 0;
  ArmedAt = CompletedAt = FirstArmAt = LastCompleteAt = 0;
  DurSumUs = GapSumUs = 0;
  DurMinUs = 0xffffffffU;
  DurMaxUs = GapMaxUs = GapCount = 0;
  ChainInCallback = (Flags & kFlagTaskRearm) == 0;
  armReset();
  ProbeRemaining = target;
  if (Flags & kFlagLoadProbe) spinStart();
  const StatSnapshot began = snapshotStats();
  uint32_t idleTicks = 0;
  while (ProbeRemaining > 0 || armQueued() > 0) {
    while (ProbeRemaining > 0 && armQueued() < ArmDepth) {
      const uint64_t remaining = ProbeRemaining;
      const uint32_t length = static_cast<uint32_t>(remaining < TransferBytes ? remaining : TransferBytes);
      const Segment seg = {Pattern, length, kSegProbe};
      if (!armPush(seg)) break;
      ProbeRemaining -= length;
    }
    if (!ChainInCallback) tryArm();
    if (xSemaphoreTake(UsbWake, pdMS_TO_TICKS(100)) == pdTRUE) {
      idleTicks = 0;
    } else if (++idleTicks >= 20 || !Vendor.mounted()) {
      break;
    }
  }
  const StatSnapshot ended = snapshotStats();
  const int64_t spinUs = ended.wall - began.wall;
  const uint32_t spin = (Flags & kFlagLoadProbe) ? spinStop() : 0;
  const int64_t active = (FirstArmAt && LastCompleteAt) ? LastCompleteAt - FirstArmAt : 0;
  const uint64_t completions = Completions;
  const int length = snprintf(
      reinterpret_cast<char *>(StatusBuffer), kStatusBytes,
      "E110_PROBE bytes=%llu target=%llu elapsed_us=%lld active_us=%lld completions=%llu xfer=%lu arm_depth=%lu "
      "chain=%u dur_min_us=%lu dur_avg_us=%lu dur_max_us=%lu dur_sum_us=%llu gap_avg_us=%lu gap_max_us=%lu gap_count=%lu "
      "arm_failures=%lu usb_core=%d idle0_us=%lu idle1_us=%lu usbd_us=%lu spin_count=%lu spin_us=%lld spin_calib_per_s=%lu "
      "gahbcfg=0x%08lx flags=0x%02x\n",
      static_cast<unsigned long long>(SentBytes), static_cast<unsigned long long>(target),
      static_cast<long long>(spinUs), static_cast<long long>(active),
      static_cast<unsigned long long>(completions), static_cast<unsigned long>(TransferBytes),
      static_cast<unsigned long>(ArmDepth), ChainInCallback ? 1U : 0U,
      static_cast<unsigned long>(DurMinUs == 0xffffffffU ? 0 : DurMinUs),
      static_cast<unsigned long>(completions ? DurSumUs / completions : 0), static_cast<unsigned long>(DurMaxUs),
      static_cast<unsigned long long>(DurSumUs),
      static_cast<unsigned long>(GapCount ? GapSumUs / GapCount : 0), static_cast<unsigned long>(GapMaxUs),
      static_cast<unsigned long>(GapCount), static_cast<unsigned long>(ArmFailures), UsbInitCore,
      static_cast<unsigned long>(ended.idle0 - began.idle0), static_cast<unsigned long>(ended.idle1 - began.idle1),
      static_cast<unsigned long>(ended.usbd - began.usbd), static_cast<unsigned long>(spin),
      static_cast<long long>(spinUs), static_cast<unsigned long>(SpinCalibPerSec),
      static_cast<unsigned long>(dwc2Register(0x08)), Flags);
  sendLine(length);
}

static void controlTask(void *) {
  runProbe(ProbeRemaining);
  RunPending = false;
  vTaskDelete(nullptr);
}

static void usbBeginTask(void *) {
  UsbInitCore = xPortGetCoreID();
  UsbReady = Device.begin(UsbConfig);
  xSemaphoreGive(UsbBeginDone);
  vTaskDelete(nullptr);
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

}  // namespace

void setup() {
  Serial.begin(115200);
  for (size_t i = 0; i < kPatternBytes; ++i) Pattern[i] = static_cast<uint8_t>(i);
  esp_cache_msync(Pattern, kPatternBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  UsbWake = xSemaphoreCreateBinary();
  StatusDone = xSemaphoreCreateBinary();
  UsbBeginDone = xSemaphoreCreateBinary();
  SpinDone = xSemaphoreCreateBinary();
  UsbConfig.vid = kVid;
  UsbConfig.pid = kPid;
  UsbConfig.manufacturer = "wch-protocols";
  UsbConfig.product = "E110 P4 USB IN ceiling";
  UsbConfig.serialNumber = "e104-p4-windows-v1";
  UsbConfig.controller = EspUsbController::HighSpeed;
  UsbConfig.webusbEnabled = true;
  xTaskCreatePinnedToCore(usbBeginTask, "e110_usb_begin", 8192, nullptr, 6, nullptr, kUsbCore);
  xSemaphoreTake(UsbBeginDone, portMAX_DELAY);
  UsbdTask = xTaskGetHandle("espusb-device");
  UsbdPriority = UsbdTask ? uxTaskPriorityGet(UsbdTask) : 0;
  spinStart();
  delay(300);
  SpinCalibPerSec = static_cast<uint32_t>(static_cast<uint64_t>(spinStop()) * 1000U / 300U);
  Serial.printf("E110_READY usb=%u usb_core=%d usbd_priority=%u gahbcfg=0x%08lx spin_calib_per_s=%lu free_internal=%u\n",
                UsbReady, UsbInitCore, static_cast<unsigned>(UsbdPriority),
                static_cast<unsigned long>(dwc2Register(0x08)), static_cast<unsigned long>(SpinCalibPerSec),
                static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
}

void loop() {
  if (RunPending || !CommandPending) {
    delay(1);
    return;
  }
  uint8_t command[kCommandBytes];
  memcpy(command, CommandMailbox, kCommandBytes);
  CommandPending = false;
  if (command[0] != 'E' || command[1] != 'P') return;
  uint32_t transfer = readLe32(command + 2);
  const uint64_t total = readLe64(command + 6);
  if (total == 0) return;
  if (transfer == 0 || transfer > kMaxTransfer) transfer = kMaxTransfer;
  transfer &= ~uint32_t(511);
  if (transfer == 0) transfer = 512;
  TransferBytes = transfer;
  Flags = command[14];
  ArmDepth = (Flags & kFlagDepthMask) + 1;
  ProbeRemaining = total;
  RunPending = true;
  xTaskCreatePinnedToCore(controlTask, "e110_ctl", 4096, nullptr, 6, nullptr, 0);
}
