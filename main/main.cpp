#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "NTC/NTC.h"
#include "Pump/pump.h"
#define onboardLED GPIO_NUM_15

static const char *mainTag = "MAIN";
static const char* MAIN_TAG = "main";

extern "C" void app_main(void) {
    ESP_LOGI(mainTag, "Starting main.\n");

    bool ledState = false;

    gpio_reset_pin(onboardLED);

    gpio_set_direction(onboardLED, GPIO_MODE_OUTPUT);

    Pump pump;
    pump.on();
    QueueHandle_t NTC_queue = xQueueCreate(10, sizeof(float));

    Thermistor NTC(NTC_queue);

    while (true) {
        bool state = pump();
        ledState = !ledState;
        gpio_set_level(onboardLED, ledState);
        ESP_LOGI(mainTag,
                 "LED: %s\nPump state: %s",
                 ledState ? "on" : "off",
                 state ? "on" : "off");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    while (true) {
        float temp;
        if (xQueueReceive(NTC_queue, &temp, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(MAIN_TAG, "Received temp data: %f\n", temp);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
