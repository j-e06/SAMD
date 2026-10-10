#include "MAX30102.h"

#include <esp_check.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "utilities.h"

static const char *TAG = "MAX30102";

static constexpr uint8_t REG_HEARTBEAT_SPO2 = 0x0C; // read 8 bytes
static constexpr uint8_t REG_COLLECT_CTRL = 0x20;   // write {0, 1} = start
static constexpr int I2C_TIMEOUT_MS = 100;
static constexpr int READ_PERIOD_MS = 1000;
esp_err_t MAX30102::begin()
{
  i2c_master_bus_config_t bus_cfg = {};
  bus_cfg.i2c_port = I2C_NUM_0;
  bus_cfg.sda_io_num = I2C_SDA;
  bus_cfg.scl_io_num = I2C_SCL;
  bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_cfg.glitch_ignore_cnt = 7;
  bus_cfg.flags.enable_internal_pullup = true;

  ESP_RETURN_ON_ERROR(
      i2c_new_master_bus(&bus_cfg, &bus_),
      TAG,
      "create i2c bus");

  const i2c_device_config_t cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = address_,
    .scl_speed_hz = 100000,
    .scl_wait_us = 0,
    .flags = 0
};

  ESP_RETURN_ON_ERROR(
      i2c_master_bus_add_device(bus_, &cfg, &dev_),
      TAG,
      "add device 0x%02X",
      address_);

  ESP_RETURN_ON_FALSE(
      xTaskCreate(
          &MAX30102::taskEntry,
          "MAX30102_task",
          4096,
          this,
          5,
          nullptr) == pdPASS,
      ESP_ERR_NO_MEM,
      TAG,
      "task create");

  return ESP_OK;
}
void MAX30102::run() {
  for (;;) {
    if (!collecting_) {
      esp_err_t err = startCollect();

      if (err == ESP_OK) {
        ESP_LOGI(TAG, "collection started");
        collecting_ = true;
      } else {
        ESP_LOGE(TAG, "startCollect failed: %s", esp_err_to_name(err));
      }

      vTaskDelay(
          pdMS_TO_TICKS(collecting_ ? READ_PERIOD_MS : 3 * READ_PERIOD_MS));
      continue;
    }

    int32_t hr, spo2;
    esp_err_t err = readPulse(&hr, &spo2);

    if (err == ESP_OK) {
      // debug statement.
      ESP_LOGI(TAG, "hr=%" PRId32 " spo2=%" PRId32, hr, spo2);

      auto e = make_entry(EventType::Pulse);
      e.pulse = {hr, spo2};

      if (!post_event(queue_, e))
        ESP_LOGE(TAG, "pulse queue full");
    } else {
      ESP_LOGE(TAG, "readPulse failed: %s", esp_err_to_name(err));
      collecting_ = false;
    }

    vTaskDelay(pdMS_TO_TICKS(READ_PERIOD_MS));
  }
}
void MAX30102::taskEntry(void *arg)
{
  static_cast<MAX30102 *>(arg)->run();
}

esp_err_t MAX30102::startCollect() {
  const uint8_t cmd[] = {REG_COLLECT_CTRL, 0x00, 0x01};
  return i2c_master_transmit(dev_, cmd, sizeof cmd, I2C_TIMEOUT_MS);
}

esp_err_t MAX30102::readPulse(int32_t *heart_rate, int32_t *spo2) {
  // just copying what DFR did with their Arduino library.
  // register pointer -> (brief stop) -> read
  const uint8_t reg = REG_HEARTBEAT_SPO2;
  uint8_t buf[8];
  esp_err_t err = i2c_master_transmit(dev_, &reg, 1, I2C_TIMEOUT_MS);
  if (err != ESP_OK)
    return err;
  err = i2c_master_receive(dev_, buf, sizeof buf, I2C_TIMEOUT_MS);
  if (err != ESP_OK)
    return err;

  // byte 0 = SpO2 %, bytes 2..5 = heart rate (32-bit big endian). Byte 1 is unused as advised by DFR

  int32_t spo2_val = buf[0];
  int32_t hr_val =
      static_cast<int32_t>((uint32_t)buf[2] << 24 | (uint32_t)buf[3] << 16 |
                           (uint32_t)buf[4] << 8 | (uint32_t)buf[5]);

  // module sends 0 if no valid reading, setting that & values that are unrealistic to -1 for simplicity.
  *spo2 = (spo2_val >= 50 && spo2_val <= 100) ? spo2_val : -1;
  *heart_rate = (hr_val >= 30 && hr_val <= 220) ? hr_val : -1;
  return ESP_OK;
}
