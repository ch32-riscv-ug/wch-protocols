// E063: does the ESP32-P4 USB 2.0 OTG HS port enumerate, and at which speed.
// Plan and report: README.ja.md
//
// The console stays on the USB-Serial-JTAG (full speed) so that flashing and
// monitoring never depend on the port under test. The device under test is a
// single CDC ACM on the OTG HS port, which the core initialises on rhport 1
// with TUSB_SPEED_HIGH.

#include <Arduino.h>
#include <HWCDC.h>
#include <USB.h>
#include <USBCDC.h>
#include <soc/soc_caps.h>

#include "esp32-hal-tinyusb.h"

#ifndef BANNER_GIT
#define BANNER_GIT "unknown"
#endif
#ifndef ARDUINO_USB_MODE
#define ARDUINO_USB_MODE (-1)
#endif
#ifndef ARDUINO_USB_CDC_ON_BOOT
#define ARDUINO_USB_CDC_ON_BOOT (-1)
#endif

// pid.codes private test range (0x0001-0x0010). 1209:0001 is reserved for E062,
// so this experiment takes a different value and leaves that device instance
// out of the host's driver cache.
static constexpr uint16_t kTestVid = 0x1209;
static constexpr uint16_t kTestPid = 0x0002;
static constexpr size_t kLineMax = 120;

// The build selects USB-OTG mode, so the core does not instantiate a global for
// the USB-Serial-JTAG port; the sketch owns one.
static HWCDC Console;

// Device under test: one CDC ACM on the OTG HS port.
static USBCDC HsCdc(0);

static bool host_armed;
static uint32_t hs_rx_bytes;
static uint32_t hs_tx_bytes;
static uint32_t hs_lines;
static char line_buffer[kLineMax + 1];
static size_t line_length;

static const char *safe_string(const char *value) {
  return (value != nullptr && value[0] != '\0') ? value : "-";
}

static void report_banner(void) {
  Console.printf("# EXP E063 v1 git=%s probe=esp32p4_usb target=none build=%s %s\n", BANNER_GIT, __DATE__, __TIME__);
}

static void report_env(void) {
  Console.printf(
    "ENV chip=%s rev=%u cores=%u flash_size=%lu psram_found=%u psram_size=%lu "
    "otg_periph=%u usb_mode=%d cdc_on_boot=%d\n",
    ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(), static_cast<unsigned long>(ESP.getFlashChipSize()),
    psramFound() ? 1U : 0U, static_cast<unsigned long>(ESP.getPsramSize()), SOC_USB_OTG_PERIPH_NUM, ARDUINO_USB_MODE,
    ARDUINO_USB_CDC_ON_BOOT
  );
}

// speed is the raw tusb_speed_t: 0 = FULL, 1 = LOW, 2 = HIGH. The value is
// printed unmapped so that an unexpected code survives into the log.
static void report_usb(void) {
  Console.printf(
    "USB vid=%04x pid=%04x serial=%s product=%s mounted=%u suspended=%u speed=%d "
    "rx=%lu tx=%lu lines=%lu\n",
    USB.VID(), USB.PID(), safe_string(USB.serialNumber()), safe_string(USB.productName()), tud_mounted() ? 1U : 0U,
    tud_suspended() ? 1U : 0U, static_cast<int>(tud_speed_get()), static_cast<unsigned long>(hs_rx_bytes),
    static_cast<unsigned long>(hs_tx_bytes), static_cast<unsigned long>(hs_lines)
  );
}

// Echo whole lines back over the HS CDC, so that a host round trip proves the
// data path and not just enumeration.
static void service_hs_echo(void) {
  while (HsCdc.available() > 0) {
    const int value = HsCdc.read();
    if (value < 0) {
      break;
    }
    ++hs_rx_bytes;
    const char received = static_cast<char>(value);
    if (received == '\r') {
      continue;
    }
    if (received != '\n') {
      if (line_length < kLineMax) {
        line_buffer[line_length++] = received;
      }
      continue;
    }
    line_buffer[line_length] = '\0';
    const size_t written = HsCdc.printf("ECHO %s\n", line_buffer);
    HsCdc.flush();
    hs_tx_bytes += written;
    ++hs_lines;
    line_length = 0;
  }
}

void setup() {
  Console.begin();

  USB.VID(kTestVid);
  USB.PID(kTestPid);
  USB.manufacturerName("Open Embedded Probe (TEST ONLY)");
  USB.productName("OEP P4 HS Enumerate Test");

  HsCdc.begin();
  USB.begin();
}

void loop() {
  service_hs_echo();

  if (!host_armed) {
    if (millis() < 1000) {
      delay(10);
      return;
    }
    while (Console.available()) {
      Console.read();
    }
    Console.println("READY E063");
    Console.flush();
    host_armed = true;
    return;
  }

  if (!Console.available()) {
    delay(2);
    return;
  }

  const int command = Console.read();
  switch (command) {
    case '?':
      report_banner();
      report_env();
      report_usb();
      break;
    case 'u': report_usb(); break;
    case 'r':
      hs_rx_bytes = 0;
      hs_tx_bytes = 0;
      hs_lines = 0;
      line_length = 0;
      Console.println("RESET counters=0");
      break;
    default: return;
  }
  Console.flush();
}
