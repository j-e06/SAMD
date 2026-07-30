#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Pump/pump.h"
#define onboardLED GPIO_NUM_15

static const char *mainTag = "MAIN";

extern "C" void app_main(void) {
    ESP_LOGI(mainTag, "Starting main.\n");

    bool ledState = false;

    gpio_reset_pin(onboardLED);

    gpio_set_direction(onboardLED, GPIO_MODE_OUTPUT);

    Pump pump;
    pump.on();

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
}
