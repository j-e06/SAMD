#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *mainTag = "MOTOR";

static constexpr gpio_num_t PUMP_IN1 = GPIO_NUM_4;
static constexpr gpio_num_t PUMP_IN2 = GPIO_NUM_5;

extern "C" void app_main()
{
    ESP_LOGI(mainTag, "Starting pump test.");

    gpio_reset_pin(PUMP_IN1);
    gpio_reset_pin(PUMP_IN2);
    gpio_set_direction(PUMP_IN1, GPIO_MODE_OUTPUT);
    gpio_set_direction(PUMP_IN2, GPIO_MODE_OUTPUT);

    // Pump OFF to start
    gpio_set_level(PUMP_IN1, 0);
    gpio_set_level(PUMP_IN2, 0);

    ESP_LOGI(mainTag, "Pump initialized and OFF.");

    while (true)
    {
        // Pump ON
        gpio_set_level(PUMP_IN1, 1);
        gpio_set_level(PUMP_IN2, 0);
        ESP_LOGI(mainTag, "Pump ON");
        vTaskDelay(pdMS_TO_TICKS(5000));

        // Pump OFF
        gpio_set_level(PUMP_IN1, 0);
        gpio_set_level(PUMP_IN2, 0);
        ESP_LOGI(mainTag, "Pump OFF");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}