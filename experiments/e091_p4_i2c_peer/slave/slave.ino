#include <driver/i2c_slave.h>

constexpr int kScl = 33, kSda = 32;
constexpr uint8_t kAddress = 0x42;
constexpr i2c_port_num_t kPort = I2C_NUM_0;

static i2c_slave_dev_handle_t slave;
static uint8_t receive_buffer[128];
static esp_err_t receive_result;

void setup() {
  Serial.begin(115200);
  i2c_slave_config_t cfg = {};
  cfg.i2c_port = kPort;
  cfg.sda_io_num = (gpio_num_t)kSda;
  cfg.scl_io_num = (gpio_num_t)kScl;
  cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  cfg.send_buf_depth = 128;
  cfg.slave_addr = kAddress;
  cfg.addr_bit_len = I2C_ADDR_BIT_LEN_7;
  esp_err_t result = i2c_new_slave_device(&cfg, &slave);
  if (result == ESP_OK) receive_result = i2c_slave_receive(slave, receive_buffer, sizeof(receive_buffer));
  Serial.printf("SLAVE-NG port=%d result=0x%x lines sda=%d scl=%d\n", kPort,
                (unsigned)result, digitalRead(kSda), digitalRead(kScl));
}

void loop() {
  static uint32_t deadline;
  if (millis() - deadline < 250) return;
  deadline = millis();
  Serial.printf("SLAVE-NG armed=0x%x first=%02x %02x %02x %02x\n",
                (unsigned)receive_result, receive_buffer[0], receive_buffer[1],
                receive_buffer[2], receive_buffer[3]);
}
