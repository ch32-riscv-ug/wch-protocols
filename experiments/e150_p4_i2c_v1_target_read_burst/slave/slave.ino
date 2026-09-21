#include <driver/i2c_slave.h>

constexpr int kScl = 33, kSda = 32;
constexpr uint8_t kAddress = 0x42;
constexpr i2c_port_num_t kPort = I2C_NUM_0;
constexpr size_t kFrameLength = 128;
constexpr size_t kWireSlotLength = kFrameLength + 1;
constexpr size_t kMaxFrames = 100;

static i2c_slave_dev_handle_t slave;
static uint8_t response[kWireSlotLength * kMaxFrames];
static esp_err_t prepare_result;
static size_t prepared_frames;

static void prepare(size_t count) {
  for (size_t frame = 0; frame < count; ++frame) {
    const size_t offset = frame * kWireSlotLength;
    for (size_t i = 0; i < kFrameLength; ++i) response[offset + i] = (uint8_t)(0x80 + i);
    // The v1 TX ring advances once more at each master-read NACK boundary.
    // Reserve that byte so the next master read starts at the next frame.
    response[offset + kFrameLength] = 0;
  }
  prepare_result = i2c_slave_transmit(slave, response, count * kWireSlotLength, 100);
  if (prepare_result == ESP_OK) prepared_frames = count;
  Serial.printf("PREP count=%u payload_bytes=%u ring_bytes=%u result=0x%x\n", (unsigned)count,
                (unsigned)(count * kFrameLength), (unsigned)(count * kWireSlotLength),
                (unsigned)prepare_result);
}

void setup() {
  Serial.begin(115200);
  i2c_slave_config_t cfg = {};
  cfg.i2c_port = kPort;
  cfg.sda_io_num = (gpio_num_t)kSda;
  cfg.scl_io_num = (gpio_num_t)kScl;
  cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  cfg.send_buf_depth = kWireSlotLength * kMaxFrames;
  cfg.slave_addr = kAddress;
  cfg.addr_bit_len = I2C_ADDR_BIT_LEN_7;
  esp_err_t result = i2c_new_slave_device(&cfg, &slave);
  Serial.printf("BURSTREAD-SLAVE init=0x%x\n", (unsigned)result);
}

void loop() {
  if (!Serial.available()) return;
  String command = Serial.readStringUntil('\n');
  unsigned count = command.substring(5).toInt();
  if (command.startsWith("PREP ") && count >= 1 && count <= kMaxFrames) prepare(count);
  else Serial.println("usage: PREP <1..100 count>");
}
