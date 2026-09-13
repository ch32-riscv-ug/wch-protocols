#include "EspUsbHost.h"

#include <esp_timer.h>

// E091 host: measure bulk OUT against the paired E091 EspUsbDevice firmware.
// The device reports its own received-byte count and pattern errors after every
// condition, so an HCD completion alone is not mistaken for delivered data.

static constexpr uint8_t VENDOR_CLASS = 0xff;
static constexpr uint8_t FILL = 0xaf;
static constexpr size_t BYTES_PER_CONDITION = 4 * 1024 * 1024;
static constexpr uint32_t TIMEOUT_MS = 10000;
static const size_t DEPTHS[] = {1, 2, 4, 8};
static const size_t SIZES[] = {512, 2048, 8192, 16384, 32768};

#ifndef E091_VENDOR_AUTO_ZLP
#define E091_VENDOR_AUTO_ZLP 0
#endif

static EspUsbHost usb;
static bool ran = false;
static uint8_t deviceAddress = 0;
static char ackLine[160];
static volatile size_t ackLength = 0;
static volatile bool ackReady = false;
static volatile uint32_t ackReceived = 0;
static volatile uint32_t ackBad = 0;
static volatile uint64_t ackElapsedUs = 0;

static uint8_t findVendorDevice()
{
  EspUsbHostDeviceInfo devices[ESP_USB_HOST_MAX_DEVICES];
  const size_t count = usb.getDevices(devices, ESP_USB_HOST_MAX_DEVICES);
  for (size_t i = 0; i < count; i++)
  {
    EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
    const size_t n = usb.getInterfaces(devices[i].address, interfaces, ESP_USB_HOST_MAX_INTERFACES);
    for (size_t j = 0; j < n; j++)
    {
      if (interfaces[j].interfaceClass == VENDOR_CLASS)
      {
        return devices[i].address;
      }
    }
  }
  return 0;
}

static bool sendBegin(uint32_t bytes)
{
  const uint8_t command[5] = {
      'B', static_cast<uint8_t>(bytes), static_cast<uint8_t>(bytes >> 8),
      static_cast<uint8_t>(bytes >> 16), static_cast<uint8_t>(bytes >> 24)};
  ackReady = false;
  ackLength = 0;
  if (!usb.vendorWrite(command, sizeof(command), deviceAddress))
  {
    return false;
  }
  delay(20);
  return true;
}

static bool requestAck()
{
  const uint8_t command = 'E';
  if (!usb.vendorWrite(&command, 1, deviceAddress))
  {
    return false;
  }
  const uint32_t deadline = millis() + 2000;
  while (!ackReady && millis() < deadline)
  {
    delay(1);
  }
  return ackReady;
}

static bool runCondition(size_t depth, size_t transferSize)
{
  if (!usb.vendorWriteQueueBegin(depth, transferSize, deviceAddress))
  {
    Serial.printf("E091_QUEUE_BEGIN_FAIL depth=%u xfer=%u error=%d\n",
                  static_cast<unsigned>(depth), static_cast<unsigned>(transferSize), usb.lastError());
    return false;
  }
  usb.vendorWriteStatsReset(deviceAddress);
  if (!sendBegin(BYTES_PER_CONDITION))
  {
    usb.vendorWriteQueueEnd(deviceAddress);
    return false;
  }

  uint32_t emptySamples = 0;
  uint32_t samples = 0;
  size_t submittedBytes = 0;
  const int64_t startedAt = esp_timer_get_time();
  while (submittedBytes < BYTES_PER_CONDITION)
  {
    if (usb.vendorWritePending(deviceAddress) == 0)
    {
      emptySamples++;
    }
    samples++;
    size_t capacity = 0;
    uint8_t *buffer = usb.vendorWriteAcquire(&capacity, TIMEOUT_MS, deviceAddress);
    if (!buffer)
    {
      break;
    }
    const size_t remaining = BYTES_PER_CONDITION - submittedBytes;
    const size_t length = min(min(transferSize, capacity), remaining);
    memset(buffer, FILL, length);
    if (!usb.vendorWriteSubmit(buffer, length, deviceAddress))
    {
      usb.vendorWriteRelease(buffer, deviceAddress);
      break;
    }
    submittedBytes += length;
  }
  const bool flushed = usb.vendorWriteFlush(TIMEOUT_MS, deviceAddress);
  const int64_t finishedAt = esp_timer_get_time();
  const EspUsbHostVendorWriteStats stats = usb.vendorWriteStats(deviceAddress);
  usb.vendorWriteQueueEnd(deviceAddress);

  const bool acknowledged = requestAck();
  const uint64_t elapsed = static_cast<uint64_t>(finishedAt - startedAt);
  const double mbps = elapsed ? (static_cast<double>(stats.bytes) * 1000000.0 / elapsed) / 1000000.0 : 0.0;
  const unsigned emptyPct = samples ? emptySamples * 100u / samples : 0;
  Serial.printf("E091_OUT depth=%u xfer=%u bytes=%llu elapsed_us=%llu mbps=%.3f "
                "completed=%lu errors=%lu queue_full=%lu empty_pct=%u "
                "device_received=%lu device_bad=%lu device_elapsed_us=%llu\n",
                static_cast<unsigned>(depth), static_cast<unsigned>(transferSize),
                static_cast<unsigned long long>(stats.bytes),
                static_cast<unsigned long long>(elapsed), mbps,
                static_cast<unsigned long>(stats.completed),
                static_cast<unsigned long>(stats.errors),
                static_cast<unsigned long>(stats.queueFullEvents), emptyPct,
                static_cast<unsigned long>(ackReceived), static_cast<unsigned long>(ackBad),
                static_cast<unsigned long long>(ackElapsedUs));

  return flushed && acknowledged && submittedBytes == BYTES_PER_CONDITION &&
         stats.bytes == BYTES_PER_CONDITION && stats.errors == 0 &&
         ackReceived == BYTES_PER_CONDITION && ackBad == 0;
}

static void runSweep()
{
  bool ok = true;
  usb.vendorSetAutoZlp(E091_VENDOR_AUTO_ZLP != 0, deviceAddress);
  for (size_t depth : DEPTHS)
  {
    for (size_t transferSize : SIZES)
    {
      ok = runCondition(depth, transferSize) && ok;
      delay(100);
    }
  }
  Serial.println(ok ? "[PASS]" : "[FAIL]");
}

void setup()
{
  Serial.begin(115200);
  delay(5000);
  Serial.println("E091_HOST_BEGIN");
  usb.onVendorData([](const EspUsbHostVendorData &data) {
    for (size_t i = 0; i < data.length && ackLength + 1 < sizeof(ackLine); i++)
    {
      const char c = static_cast<char>(data.data[i]);
      if (c == '\n')
      {
        ackLine[ackLength] = 0;
        unsigned long received = 0, target = 0, bad = 0;
        unsigned long long elapsed = 0;
        if (sscanf(ackLine, "E091_ACK received=%lu target=%lu bad_words=%lu elapsed_us=%llu",
                   &received, &target, &bad, &elapsed) == 4)
        {
          ackReceived = static_cast<uint32_t>(received);
          ackBad = static_cast<uint32_t>(bad);
          ackElapsedUs = static_cast<uint64_t>(elapsed);
          ackReady = true;
        }
        ackLength = 0;
      }
      else
      {
        ackLine[ackLength++] = c;
      }
    }
  });
  usb.begin();
}

void loop()
{
  if (!ran)
  {
    deviceAddress = findVendorDevice();
    if (deviceAddress)
    {
      ran = true;
      if (!usb.vendorOpen(deviceAddress))
      {
        Serial.println("[FAIL]");
        return;
      }
      Serial.printf("E091_OPEN out_ep=0x%02x out_mps=%u\n",
                    usb.vendorOutEndpoint(deviceAddress), usb.vendorOutPacketSize(deviceAddress));
      runSweep();
    }
    else if (millis() > 60000)
    {
      ran = true;
      Serial.println("[FAIL]");
    }
  }
  delay(10);
}
