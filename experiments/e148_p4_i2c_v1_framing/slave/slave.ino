#include <driver/i2c_slave.h>

constexpr int kScl = 33, kSda = 32;
constexpr uint8_t kAddress = 0x42;
constexpr i2c_port_num_t kPort = I2C_NUM_0;
constexpr size_t kMaxPayload = 128;

enum class ReceiveState : uint8_t { Header, Payload };
static i2c_slave_dev_handle_t slave;
static uint8_t receive_buffer[kMaxPayload];
static volatile bool receive_complete;
static ReceiveState state = ReceiveState::Header;
static size_t armed_length = 1;
static uint32_t frames;
static uint32_t pattern_errors;
static uint8_t last_length;
static uint8_t last_sum;
static bool last_pattern_ok;
static esp_err_t arm_result;

static bool receive_done(i2c_slave_dev_handle_t,
                         const i2c_slave_rx_done_event_data_t *, void *) {
  receive_complete = true;
  return false;
}

static esp_err_t arm_receive(size_t length) {
  armed_length = length;
  return i2c_slave_receive(slave, receive_buffer, length);
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
  if (result == ESP_OK) arm_result = arm_receive(1);
  Serial.printf("FRAMED-SLAVE init=0x%x arm=0x%x\n", (unsigned)result, (unsigned)arm_result);
}

void loop() {
  if (receive_complete) {
    receive_complete = false;
    if (state == ReceiveState::Header) {
      last_length = receive_buffer[0];
      if (last_length == 0 || last_length > kMaxPayload) {
        state = ReceiveState::Header;
        arm_result = arm_receive(1);
      } else {
        state = ReceiveState::Payload;
        arm_result = arm_receive(last_length);
      }
    } else {
      last_sum = 0;
      last_pattern_ok = true;
      for (size_t i = 0; i < armed_length; ++i) {
        last_sum += receive_buffer[i];
        if (receive_buffer[i] != static_cast<uint8_t>(0x11 + i)) last_pattern_ok = false;
      }
      if (!last_pattern_ok) ++pattern_errors;
      ++frames;
      state = ReceiveState::Header;
      arm_result = arm_receive(1);
    }
  }
  static uint32_t deadline;
  if (millis() - deadline < 250) return;
  deadline = millis();
  Serial.printf("FRAMED-SLAVE frames=%lu pattern_errors=%lu last_ok=%d state=%s armed=%u last_len=%u sum=%02x arm=0x%x\n",
                (unsigned long)frames, (unsigned long)pattern_errors, last_pattern_ok,
                state == ReceiveState::Header ? "header" : "payload", (unsigned)armed_length,
                (unsigned)last_length, last_sum, (unsigned)arm_result);
}
