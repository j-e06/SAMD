#include "pump.h"
#include "esp_log.h"

static const char *TAG = "Pump";

// based on pumps duty rating, pump can't run longer than this on without a manual "rearming" of the pump
static constexpr int64_t MAX_ON_US = 60 * 1000 * 1000;

Pump::Pump() {
  // insta turn off pump, can't trust it.
  gpio_set_level(PUMP_IN1, 0);
  gpio_set_level(PUMP_IN2, 0);

  // input_output, so we can get hte level back
  const gpio_config_t io = {
      .pin_bit_mask = (1ULL << PUMP_IN1) | (1ULL << PUMP_IN2),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  if (gpio_config(&io) != ESP_OK) {
    ESP_LOGE(TAG, "gpio config failed");
    return;
  }

  // creating fail safe, duh
  esp_timer_create_args_t args = {};
  args.callback = &Pump::failsafeCb;
  args.arg = this;
  args.name = "pump_failsafe";
  if (esp_timer_create(&args, &failsafe_) != ESP_OK) {
    ESP_LOGE(TAG, "failsafe timer create failed");
    failsafe_ = nullptr;
  }
}
esp_err_t Pump::on() {
  if (!failsafe_) {
    ESP_LOGE(TAG, "no failsafe timer, refusing to run");
    return ESP_ERR_INVALID_STATE;
  }
  ESP_LOGI(TAG, "pump on");

  // if failsafe can't be armed, pump doesn't get ran.
  esp_timer_stop(failsafe_); // return error if not running, doesn't matter
  esp_err_t err = esp_timer_start_once(failsafe_, MAX_ON_US);
  if (err != ESP_OK) {
    off();
    return err;
  }
  gpio_set_level(PUMP_IN1, 1);
  return ESP_OK;
}

void Pump::off() {
  ESP_LOGI(TAG, "pump off");
  if (failsafe_)
    esp_timer_stop(failsafe_);
  gpio_set_level(PUMP_IN1, 0);
}

void Pump::failsafeCb(void *arg) {
  ESP_LOGW(TAG, "max on-time reached, stopping");
  static_cast<Pump *>(arg)->off();
}
