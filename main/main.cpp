#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "NTC/NTC.h"
#include "Pump/pump.h"

#define onboardLED GPIO_NUM_15
#define LED_PIN GPIO_NUM_17

#define TOGGLE_SWITCH GPIO_NUM_16

static const char* MAIN_TAG = "main";

extern "C" void app_main(void) {
    ESP_LOGI(MAIN_TAG, "Starting main.\n");

    // bool ledState = false;

    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

    gpio_reset_pin(TOGGLE_SWITCH);
    gpio_set_direction(TOGGLE_SWITCH, GPIO_MODE_INPUT);
    gpio_set_pull_mode(TOGGLE_SWITCH, GPIO_PULLUP_ONLY);
    Pump pump; // default off.
    QueueHandle_t NTC_queue = xQueueCreate(10, sizeof(float));

    Thermistor NTC(NTC_queue);
    bool pumpOn = false;
    bool prevState = false;
    while (true) {
        // read temp.
        float temp;
        if (xQueueReceive(NTC_queue, &temp, pdMS_TO_TICKS(10)) == pdTRUE) {
            ESP_LOGI(MAIN_TAG, "Received temp data: %f\n", temp);
        }
        int curState = !gpio_get_level(TOGGLE_SWITCH);

        if (curState && !prevState) {
            pumpOn = !pumpOn;
            if (pumpOn) {
                pump.on();
                gpio_set_level(LED_PIN, 1);
            }else {
                pump.off();
                gpio_set_level(LED_PIN, 0);
            }
        }
        prevState = curState;
        bool s = pump();
        ESP_LOGI(MAIN_TAG, "Current state: %d | Pump state: %d", curState, s);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

}
