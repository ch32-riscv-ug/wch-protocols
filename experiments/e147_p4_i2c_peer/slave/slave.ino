#include <driver/i2c_slave.h>

constexpr int kScl = 33, kSda = 32;
constexpr uint8_t kAddress = 0x42;
constexpr i2c_port_num_t kPort = I2C_NUM_0;
#ifndef E091_WRITE_LENGTH
#define E091_WRITE_LENGTH 4
#endif
constexpr size_t kWriteLength = E091_WRITE_LENGTH;
static_assert(kWriteLength >= 1 && kWriteLength <= 128, "E091_WRITE_LENGTH must be 1..128");

static i2c_slave_dev_handle_t slave;
static uint8_t receive_buffer[kWriteLength];
static esp_err_t receive_result;
static volatile bool receive_complete;
static volatile uint32_t transactions;
static uint8_t received_sum;

static uint8_t received_at(size_t index) {
  return index < kWriteLength ? receive_buffer[index] : 0;
}

static bool receive_done(i2c_slave_dev_handle_t,
                         const i2c_slave_rx_done_event_data_t *, void *) {
  ++transactions;
  receive_complete = true;
  return false;
}

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
  if (result == ESP_OK) {
    i2c_slave_event_callbacks_t callbacks = {};
    callbacks.on_recv_done = receive_done;
    result = i2c_slave_register_event_callbacks(slave, &callbacks, nullptr);
  }
  if (result == ESP_OK) receive_result = i2c_slave_receive(slave, receive_buffer, sizeof(receive_buffer));
  Serial.printf("SLAVE-NG port=%d result=0x%x lines sda=%d scl=%d\n", kPort,
                (unsigned)result, digitalRead(kSda), digitalRead(kScl));
}

void loop() {
  if (receive_complete) {
    receive_complete = false;
    received_sum = 0;
    for (size_t i = 0; i < kWriteLength; ++i) received_sum += receive_buffer[i];
    receive_result = i2c_slave_receive(slave, receive_buffer, sizeof(receive_buffer));
  }
  static uint32_t deadline;
  if (millis() - deadline < 250) return;
  deadline = millis();
  Serial.printf("SLAVE-NG length=%u transactions=%lu armed=0x%x sum=%02x data=%02x %02x %02x %02x\n",
                (unsigned)kWriteLength, (unsigned long)transactions, (unsigned)receive_result,
                received_sum,
                received_at(0), received_at(1), received_at(2), received_at(3));
}
