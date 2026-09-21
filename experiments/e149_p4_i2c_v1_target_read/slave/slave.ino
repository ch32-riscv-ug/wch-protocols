#include <driver/i2c_slave.h>

constexpr int kScl = 33, kSda = 32;
constexpr uint8_t kAddress = 0x42;
constexpr i2c_port_num_t kPort = I2C_NUM_0;
constexpr size_t kMaxLength = 128;

static i2c_slave_dev_handle_t slave;
static uint8_t response[kMaxLength];
static uint32_t prepared_bytes;
static esp_err_t prepare_result;

static void prepare(size_t length) {
  for (size_t i = 0; i < length; ++i) response[i] = static_cast<uint8_t>(0x80 + i);
  prepare_result = i2c_slave_transmit(slave, response, length, 100);
  if (prepare_result == ESP_OK) prepared_bytes += length;
  Serial.printf("PREP length=%u result=0x%x prepared_total=%lu\n", (unsigned)length,
                (unsigned)prepare_result, (unsigned long)prepared_bytes);
}

void setup() {
  Serial.begin(115200);
  i2c_slave_config_t cfg = {};
  cfg.i2c_port = kPort;
  cfg.sda_io_num = (gpio_num_t)kSda;
  cfg.scl_io_num = (gpio_num_t)kScl;
  cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  cfg.send_buf_depth = 256;
  cfg.slave_addr = kAddress;
  cfg.addr_bit_len = I2C_ADDR_BIT_LEN_7;
  esp_err_t result = i2c_new_slave_device(&cfg, &slave);
  Serial.printf("READ-SLAVE init=0x%x\n", (unsigned)result);
}

void loop() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    unsigned length = command.substring(5).toInt();
    if (command.startsWith("PREP ") && length >= 1 && length <= kMaxLength) prepare(length);
    else Serial.println("usage: PREP <1..128 length>");
  }
  static uint32_t deadline;
  if (millis() - deadline < 250) return;
  deadline = millis();
  Serial.printf("READ-SLAVE prepared_total=%lu last_result=0x%x\n",
                (unsigned long)prepared_bytes, (unsigned)prepare_result);
}
