#define E129_EMBEDDED
#include "../e129_swio_only_cpu_boot/e129_swio_only_cpu_boot.ino"
#include <Wire.h>
#include "driver/spi_slave.h"

// GPIO1/GPIO3 are the fixture's UART and GPIO6..GPIO11 are the ESP32 flash.
// All remaining bonded GPIOs are inputs while scanning.  GPIO34..GPIO39 do
// not have internal pulls, but are retained because a driven HIGH is still
// observable there.
static const uint8_t kFixturePins[] = {
  0, 4, 5, 13, 14, 17, 18, 19,
  21, 22, 25, 26, 27, 32, 33, 34, 35, 36, 39,
};
static HardwareSerial gDutSerial(2);
static bool gDutSerialStarted = false;
static bool gDutLineStart = true;
static volatile uint32_t gI2cReceives = 0;
static volatile uint8_t gI2cData[8] = {};
static volatile uint8_t gI2cLength = 0;
static bool gI2cStarted = false;
static volatile uint32_t gSpiTransfers = 0;
static volatile uint8_t gSpiRx[4] = {};
static bool gSpiStarted = false;

/* E129's V command is intentionally primitive.  This fixture is also used to
 * preserve/compare a damaged target, and the long flying SWIO lead has shown
 * rare one-bit read errors.  Take seven successful samples per word and vote
 * each bit independently. */
static void receiveReadPageVoted() {
  uint8_t packet[4];
  const size_t received = Serial.readBytes(packet, sizeof(packet));
  if (received != sizeof(packet)) {
    Serial.printf("VOTE STOP reason=short_packet received=%u expected=%u\n",
                  (unsigned)received, (unsigned)sizeof(packet));
    return;
  }
  const uint32_t address = (uint32_t)packet[0] |
                           ((uint32_t)packet[1] << 8) |
                           ((uint32_t)packet[2] << 16) |
                           ((uint32_t)packet[3] << 24);
  const int c = 10;
  if ((address & 63u) || !attachHaltWriter(c)) {
    Serial.println("VOTE STOP reason=prepare_failed");
    return;
  }
  uint32_t page[16];
  unsigned unstableSamples = 0;
  for (uint32_t wordIndex = 0; wordIndex < 16; ++wordIndex) {
    uint32_t samples[7];
    unsigned valid = 0;
    for (unsigned attempt = 0; attempt < 40 && valid < 7; ++attempt) {
      uint32_t value = 0;
      if (!readMemoryWord(address + wordIndex * 4u, &value, c)) {
        samples[valid++] = value;
      }
    }
    if (valid != 7) {
      Serial.printf("VOTE STOP reason=read_failed offset=%lu valid=%u\n",
                    (unsigned long)(wordIndex * 4u), valid);
      return;
    }
    uint32_t voted = 0;
    for (unsigned bit = 0; bit < 32; ++bit) {
      unsigned ones = 0;
      for (unsigned sample = 0; sample < 7; ++sample) {
        ones += (samples[sample] >> bit) & 1u;
      }
      if (ones >= 4) voted |= 1u << bit;
    }
    for (unsigned sample = 0; sample < 7; ++sample) {
      unstableSamples += samples[sample] != voted;
    }
    page[wordIndex] = voted;
  }
  Serial.printf("VOTE PAGE address=0x%08lx unstable=%u",
                (unsigned long)address, unstableSamples);
  for (uint32_t word : page) Serial.printf(" %08lx", (unsigned long)word);
  Serial.println("\nVOTE OK");
}

static bool isFixturePin(int pin) {
  for (uint8_t candidate : kFixturePins) {
    if (candidate == pin) return true;
  }
  return false;
}

static void fixtureInputs() {
  if (gDutSerialStarted) {
    gDutSerial.end();
    gDutSerialStarted = false;
  }
  for (uint8_t pin : kFixturePins) {
    // Idle must be genuinely high impedance.  Every UIAP pin is wired to this
    // fixture, including the software-USB pair; a permanent ESP32 pull-down on
    // either USB line is enough to break enumeration.
    pinMode(pin, INPUT);
  }
}

static void fixtureScanInputs() {
  fixtureInputs();
  for (uint8_t pin : kFixturePins) {
    pinMode(pin, pin >= 34 ? INPUT : INPUT_PULLDOWN);
  }
}

static uint32_t fixtureMask() {
  uint32_t mask = 0;
  for (size_t index = 0; index < sizeof(kFixturePins); ++index) {
    if (digitalRead(kFixturePins[index])) mask |= 1u << index;
  }
  return mask;
}

static void releaseControlPins() {
  // Mapping and peripheral tests must not bias either control line.  GPIO16
  // is driven only inside an explicitly requested SWIO operation; GPIO23 is
  // never driven by this experiment.
  pinMode(16, INPUT);
  pinMode(23, INPUT);
}

static void printFixtureMask(uint32_t mask) {
  Serial.printf("MAP t=%lu mask=%08lx high=", millis(), (unsigned long)mask);
  bool first = true;
  for (size_t index = 0; index < sizeof(kFixturePins); ++index) {
    if (!(mask & (1u << index))) continue;
    if (!first) Serial.print(',');
    Serial.print(kFixturePins[index]);
    first = false;
  }
  if (first) Serial.print('-');
  Serial.println();
}

static void scanFixture(unsigned long durationMs) {
  fixtureScanInputs();
  Serial.printf("MAP BEGIN duration_ms=%lu pins=", durationMs);
  for (size_t index = 0; index < sizeof(kFixturePins); ++index) {
    if (index) Serial.print(',');
    Serial.print(kFixturePins[index]);
  }
  Serial.println();

  uint32_t previous = ~fixtureMask();
  const unsigned long started = millis();
  while (millis() - started < durationMs) {
    const uint32_t current = fixtureMask();
    if (current != previous) {
      printFixtureMask(current);
      previous = current;
    }
    delay(1);
  }
  Serial.println("MAP END");
  fixtureInputs();
}

static void measureEdges() {
  const String request = Serial.readStringUntil('\n');
  int pin = -1;
  unsigned durationMs = 0;
  if (sscanf(request.c_str(), "%d %u", &pin, &durationMs) != 2 ||
      !isFixturePin(pin) || durationMs == 0 || durationMs > 5000) {
    Serial.printf("EDGE ERROR request=%s\n", request.c_str());
    return;
  }
  if (gDutSerialStarted && (pin == 21 || pin == 22)) {
    gDutSerial.end();
    gDutSerialStarted = false;
  }
  pinMode(pin, INPUT);
  unsigned rises = 0;
  unsigned falls = 0;
  uint32_t highUs = 0;
  int previous = digitalRead(pin);
  uint32_t segmentStarted = micros();
  const uint32_t started = segmentStarted;
  const uint32_t requestedUs = durationMs * 1000u;
  while ((uint32_t)(micros() - started) < requestedUs) {
    const int current = digitalRead(pin);
    if (current == previous) continue;
    const uint32_t now = micros();
    if (previous) highUs += now - segmentStarted;
    if (current) ++rises;
    else ++falls;
    previous = current;
    segmentStarted = now;
  }
  const uint32_t ended = micros();
  if (previous) highUs += ended - segmentStarted;
  Serial.printf("EDGE gpio=%d elapsed_us=%lu rises=%u falls=%u high_us=%lu\n",
                pin, (unsigned long)(ended - started), rises, falls,
                (unsigned long)highUs);
}

static void startDutSerial() {
  if (gDutSerialStarted) return;
  // Measured mapping: V003 PD5/TX -> GPIO22, PD6/RX <- GPIO21.
  // These pads may just have been driven for the ADC test.  Explicitly remove
  // GPIO output enable before attaching the UART matrix, otherwise GPIO22 can
  // contend with the DUT TX signal even though HardwareSerial was restarted.
  pinMode(22, INPUT);
  pinMode(21, INPUT);
  gDutSerial.begin(115200, SERIAL_8N1, 22, 21);
  gDutSerialStarted = true;
  Serial.println("UART READY rx=22 tx=21 baud=115200");
}

static void configureFixturePin() {
  String request = Serial.readStringUntil('\n');
  int pin = -1;
  char mode = 0;
  if (sscanf(request.c_str(), "%d %c", &pin, &mode) != 2 ||
      !isFixturePin(pin)) {
    Serial.printf("PIN ERROR request=%s\n", request.c_str());
    return;
  }
  if (gDutSerialStarted && (pin == 21 || pin == 22)) {
    gDutSerial.end();
    gDutSerialStarted = false;
  }
  switch (mode) {
    case 'Z': pinMode(pin, INPUT); break;
    case 'D': pinMode(pin, INPUT_PULLDOWN); break;
    case 'U': pinMode(pin, INPUT_PULLUP); break;
    case 'B': pinMode(pin, INPUT | PULLUP | PULLDOWN); break;
    case 'C':
      if (pin != 25 && pin != 26) {
        Serial.printf("PIN ERROR mode=C requires_gpio=25_or_26\n");
        return;
      }
      dacWrite(pin, 128);
      break;
    case 'L': pinMode(pin, OUTPUT); digitalWrite(pin, LOW); break;
    case 'H': pinMode(pin, OUTPUT); digitalWrite(pin, HIGH); break;
    default:
      Serial.printf("PIN ERROR mode=%c\n", mode);
      return;
  }
  // Do not call analogReadMilliVolts() here: on ESP32 it changes the same pad
  // to analog mode and silently removes the drive/pulls we just configured.
  Serial.printf("PIN OK gpio=%d mode=%c read=%d\n", pin, mode,
                digitalRead(pin));
}

static void sendDutLine() {
  startDutSerial();
  const String line = Serial.readStringUntil('\n');
  gDutSerial.println(line);
  Serial.printf("UART TX %s\n", line.c_str());
}

static void forwardDutSerial() {
  if (!gDutSerialStarted) return;
  // Bound work per loop so a broken/chatty DUT cannot starve fixture commands.
  for (unsigned forwarded = 0;
       forwarded < 128 && gDutSerial.available(); ++forwarded) {
    const int value = gDutSerial.read();
    if (gDutLineStart) {
      Serial.print("DUT ");
      gDutLineStart = false;
    }
    Serial.write(value);
    if (value == '\n') gDutLineStart = true;
  }
}

static void i2cReceive(int count) {
  uint8_t length = 0;
  while (Wire.available() && length < sizeof(gI2cData)) {
    gI2cData[length++] = Wire.read();
  }
  while (Wire.available()) Wire.read();
  gI2cLength = length;
  ++gI2cReceives;
  (void)count;
}

static void i2cRequest() {
  const uint8_t reply[] = {0xde, 0xad, 0xbe, 0xef};
  Wire.write(reply, sizeof(reply));
}

static void startI2cSlave() {
  if (gI2cStarted) return;
  Wire.onReceive(i2cReceive);
  Wire.onRequest(i2cRequest);
  gI2cStarted = Wire.begin((uint8_t)0x42, 19, 18, 100000);
  // Classic ESP32 requires an initial slave buffer before the first request;
  // its own WireSlave example does this in addition to onRequest().
  const uint8_t reply[] = {0xde, 0xad, 0xbe, 0xef};
  if (gI2cStarted) Wire.slaveWrite(reply, sizeof(reply));
  Serial.printf("I2C %s address=0x42 sda=19 scl=18\n",
                gI2cStarted ? "READY" : "ERROR");
}

static void printI2cStatus() {
  Serial.printf("I2C STATUS receives=%lu length=%u data=",
                (unsigned long)gI2cReceives, (unsigned)gI2cLength);
  for (uint8_t i = 0; i < gI2cLength; ++i) Serial.printf("%02x", gI2cData[i]);
  Serial.println();
}

static void spiSlaveTask(void *) {
  static const uint8_t tx[4] = {0xc3, 0x5a, 0x69, 0x96};
  for (;;) {
    spi_slave_transaction_t transaction = {};
    uint8_t rx[4] = {};
    transaction.length = 32;
    transaction.tx_buffer = tx;
    transaction.rx_buffer = rx;
    if (spi_slave_transmit(SPI2_HOST, &transaction, portMAX_DELAY) == ESP_OK) {
      for (size_t i = 0; i < sizeof(rx); ++i) gSpiRx[i] = rx[i];
      ++gSpiTransfers;
    }
  }
}

static void startSpiSlave(uint8_t mode) {
  if (gSpiStarted) return;
  if (gI2cStarted) {
    Wire.end();
    gI2cStarted = false;
  }
  // Strap-safe wiring measured on 2026-09-19:
  // PC5/SCK=GPIO27, PC6/MOSI=GPIO4, PC7/MISO=GPIO14.
  spi_bus_config_t bus = {};
  bus.mosi_io_num = 4;
  bus.miso_io_num = 14;
  bus.sclk_io_num = 27;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = 4;
  spi_slave_interface_config_t slave = {};
  slave.spics_io_num = 19;
  slave.queue_size = 1;
  slave.mode = mode & 3u;
  const esp_err_t result = spi_slave_initialize(
      SPI2_HOST, &bus, &slave, SPI_DMA_DISABLED);
  if (result == ESP_OK) {
    gSpiStarted = true;
    xTaskCreate(spiSlaveTask, "uiap-spi", 2048, nullptr, 2, nullptr);
  }
  Serial.printf("SPI %s mode=%u sck=27 mosi=4 miso=14 cs=19 status=%d\n",
                gSpiStarted ? "READY" : "ERROR", mode & 3u, (int)result);
}

static void printSpiStatus() {
  Serial.printf("SPI STATUS transfers=%lu data=%02x%02x%02x%02x\n",
                (unsigned long)gSpiTransfers, gSpiRx[0], gSpiRx[1],
                gSpiRx[2], gSpiRx[3]);
}

void setup() {
  fixtureInputs();
  e129_setup();
  releaseControlPins();
}

void loop() {
  forwardDutSerial();
  if (!Serial.available()) return;
  const int command = Serial.read();
  if (command == '?') {
    Serial.println("# EXP E132 UIAPduino pin-map fixture");
    Serial.println("READY commands=NBRHSWVvGMPEUTIKJjQX");
  } else if (command == 'G') {
    // One DUT cycle is 12 s.  Two cycles make phase-independent decoding
    // straightforward and expose unstable or multiply-connected inputs.
    scanFixture(32000);
  } else if (command == 'M') {
    fixtureInputs();
    printFixtureMask(fixtureMask());
  } else if (command == 'P') {
    configureFixturePin();
  } else if (command == 'E') {
    measureEdges();
  } else if (command == 'U') {
    startDutSerial();
  } else if (command == 'T') {
    sendDutLine();
  } else if (command == 'I') {
    startI2cSlave();
  } else if (command == 'K') {
    printI2cStatus();
  } else if (command == 'J') {
    startSpiSlave(0);
  } else if (command == 'j') {
    const String request = Serial.readStringUntil('\n');
    startSpiSlave((uint8_t)request.toInt());
  } else if (command == 'Q') {
    printSpiStatus();
  } else if (command == 'X') {
    Serial.println("FIXTURE RESTART");
    Serial.flush();
    ESP.restart();
  } else {
    // Reuse the proven E129 SWIO boot/read/write primitives.
    if (command == 'N') {
      Serial.println("NORMALIZE BEGIN");
      if (normalizeByCpuReset()) Serial.println("NORMALIZE END");
      else Serial.println("NORMALIZE FAILED");
      releaseControlPins();
    } else if (command == 'R') {
      Serial.println("RESET ASSERT pin=23"); Serial.flush();
      hardwareResetTarget();
      Serial.println("RESET RELEASE pin=23");
      releaseControlPins();
    } else if (command == 'H') {
      Serial.println("HW BOOT BEGIN");
      cpuSeamlessBoot();
      Serial.println("HW BOOT END");
      releaseControlPins();
    } else if (command == 'B') {
      // Boot entry must be repeatable from either the application or a stale
      // bootloader state. First return to an ordinary user reset, allow that
      // reset to complete, then request the product bootloader.
      Serial.println("SWIO BOOT NORMALIZE BEGIN");
      if (!normalizeByCpuReset()) {
        Serial.println("SWIO BOOT NORMALIZE FAILED");
        releaseControlPins();
        return;
      }
      delay(1500);
      releaseControlPins();
      Serial.println("SWIO BOOT BEGIN"); swioOnlyBoot(); Serial.println("SWIO BOOT END");
      // The injected payload resets after its delay loop. Keep SWIO at the
      // protocol idle level until that operation has completed, then go Hi-Z.
      delay(1500);
      releaseControlPins();
    } else if (command == 'S') {
      inspectBootStatus();
      releaseControlPins();
    } else if (command == 'W') {
      receiveFlashPage();
      releaseControlPins();
    } else if (command == 'V') {
      receiveReadPage();
      releaseControlPins();
    } else if (command == 'v') {
      receiveReadPageVoted();
      releaseControlPins();
    }
  }
}
