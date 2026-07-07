#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PUMP_IN1 GPIO_NUM_17
#define PUMP_IN2 GPIO_NUM_16

extern "C" void app_main(void) {
  gpio_config_t io_conf = {};
  io_conf.pin_bit_mask = (1ULL << PUMP_IN1) | (1ULL << PUMP_IN2);
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.intr_type = GPIO_INTR_DISABLE;

  ESP_ERROR_CHECK(gpio_config(&io_conf));

  while (true) {
    // Forward
    gpio_set_level(PUMP_IN1, 1);
    gpio_set_level(PUMP_IN2, 0);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Coast (off)as
    // gpio_set_level(PUMP_IN1, 0);
    // gpio_set_level(PUMP_IN2, 0);
    // vTaskDelay(pdMS_TO_TICKS(5000));
  }
}
