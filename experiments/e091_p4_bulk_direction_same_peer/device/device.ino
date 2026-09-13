#include "EspUsbDevice.h"

#include <esp_timer.h>

// E091 peer: accept a measured bulk OUT stream, drain it outside TinyUSB's RX
// completion callback, and report the device-side byte count back over bulk IN.
// The large RX ring is intentional: it separates USB re-arm latency from the
// sketch's loop scheduling while retaining the normal buffered vendor API.

EspUsbDevice device;
EspUsbDeviceVendor Vendor(device);

static constexpr uint8_t FILL = 0xaf;
static uint8_t drainBuffer[8192];
static bool receiving = false;
static bool complete = false;
static uint32_t targetBytes = 0;
static uint32_t receivedBytes = 0;
static uint32_t badWords = 0;
static int64_t firstByteAt = 0;
static int64_t lastByteAt = 0;

static uint32_t readLe32(const uint8_t *p)
{
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

static void sendAck()
{
  char line[160];
  const long long elapsed = firstByteAt && lastByteAt >= firstByteAt
                                ? static_cast<long long>(lastByteAt - firstByteAt)
                                : 0;
  const int length = snprintf(line, sizeof(line),
                              "E091_ACK received=%lu target=%lu bad_words=%lu elapsed_us=%lld\n",
                              static_cast<unsigned long>(receivedBytes),
                              static_cast<unsigned long>(targetBytes),
                              static_cast<unsigned long>(badWords), elapsed);
  Vendor.write(reinterpret_cast<const uint8_t *>(line), static_cast<size_t>(length));
  Vendor.flush();
}

static void drainPayload()
{
  while (receiving && Vendor.available() > 0)
  {
    const size_t available = static_cast<size_t>(Vendor.available());
    const size_t want = min(available, sizeof(drainBuffer));
    const size_t got = Vendor.read(drainBuffer, want);
    if (got == 0)
    {
      break;
    }
    const int64_t now = esp_timer_get_time();
    if (receivedBytes == 0)
    {
      firstByteAt = now;
    }
    lastByteAt = now;

    // Four bytes per comparison keeps validation cheap enough not to turn the
    // pattern checker into the USB limit. The tail is checked byte-wise.
    size_t i = 0;
    for (; i + sizeof(uint32_t) <= got; i += sizeof(uint32_t))
    {
      uint32_t word;
      memcpy(&word, drainBuffer + i, sizeof(word));
      if (word != 0xafafafafu)
      {
        badWords++;
      }
    }
    for (; i < got; i++)
    {
      if (drainBuffer[i] != FILL)
      {
        badWords++;
      }
    }
    receivedBytes += static_cast<uint32_t>(got);
    if (receivedBytes >= targetBytes)
    {
      receiving = false;
      complete = receivedBytes == targetBytes;
    }
  }
}

static void processCommands()
{
  if (receiving || Vendor.available() == 0)
  {
    return;
  }
  uint8_t command[16];
  const size_t got = Vendor.read(command, min(static_cast<size_t>(Vendor.available()), sizeof(command)));
  if (got == 5 && command[0] == 'B')
  {
    targetBytes = readLe32(command + 1);
    receivedBytes = 0;
    badWords = 0;
    firstByteAt = 0;
    lastByteAt = 0;
    complete = false;
    receiving = targetBytes != 0;
  }
  else if (got == 1 && command[0] == 'E')
  {
    sendAck();
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4020;
  config.manufacturer = "wch-protocols";
  config.product = "E091 bidirectional vendor peer";
  config.serialNumber = "e091-p4-bulk-direction";
  Serial.printf("E091_DEVICE_BEGIN ok=%u rx_capacity=%u\n",
                device.begin(config) ? 1 : 0,
                static_cast<unsigned>(CFG_TUD_VENDOR_RX_BUFSIZE));
}

void loop()
{
  drainPayload();
  processCommands();
  if (Serial.available() && Serial.read() == '?')
  {
    Serial.printf("E091_DEVICE_STATUS receiving=%u complete=%u received=%lu target=%lu bad_words=%lu\n",
                  receiving ? 1 : 0, complete ? 1 : 0,
                  static_cast<unsigned long>(receivedBytes),
                  static_cast<unsigned long>(targetBytes),
                  static_cast<unsigned long>(badWords));
  }
  delay(0);
}
