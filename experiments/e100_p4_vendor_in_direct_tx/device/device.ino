#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceVendor Vendor(device);

static constexpr size_t CHUNK_SIZE = 8192;
static volatile size_t streamRemaining = 0;
static uint8_t streamNext = 0;
static uint8_t chunkBuffer[CHUNK_SIZE];

extern "C" void esp_usb_device_direct_vendor_rx(const uint8_t *data, uint32_t length)
{
  if (length >= 5 && data[0] == 'S')
  {
    streamRemaining = static_cast<uint32_t>(data[1]) |
                      (static_cast<uint32_t>(data[2]) << 8) |
                      (static_cast<uint32_t>(data[3]) << 16) |
                      (static_cast<uint32_t>(data[4]) << 24);
    streamNext = 0;
  }
}

static void processStream()
{
  while (streamRemaining > 0)
  {
    const size_t want = streamRemaining < CHUNK_SIZE ? streamRemaining : CHUNK_SIZE;
    if (!Vendor.waitWritable(want, 100))
    {
      return;
    }
    for (size_t i = 0; i < want; i++)
    {
      chunkBuffer[i] = streamNext++;
    }
    const size_t written = Vendor.write(chunkBuffer, want);
    streamRemaining -= written;
    if (written < want)
    {
      streamNext = static_cast<uint8_t>(streamNext - (want - written));
      return;
    }
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4019;
  config.manufacturer = "EspUsbDevice";
  config.product = "E100 direct TX vendor";
  config.serialNumber = "e100-p4-direct-tx";
  Serial.printf("E100_DEVICE_BEGIN ok=%u tx_xfer=%u buffered=%u\n",
                device.begin(config) ? 1 : 0,
                static_cast<unsigned>(CFG_TUD_VENDOR_TX_EPSIZE),
                static_cast<unsigned>(CFG_TUD_VENDOR_TXRX_BUFFERED));
}

void loop()
{
  processStream();
  delay(0);
}
