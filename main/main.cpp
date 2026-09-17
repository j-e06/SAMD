#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt/MQTTHandler.h"
#include "structs.h"
#include "nvs/NvsHandler.h"
#include "wifi/WifiHandler.h"
#define onboardLED GPIO_NUM_15

static const char *mainTag = "MAIN";

extern "C" void app_main(void) {
    ESP_LOGI(mainTag, "Starting main.\n");

    bool ledState = false;

    gpio_reset_pin(onboardLED);

    gpio_set_direction(onboardLED, GPIO_MODE_OUTPUT);

    // TODO: initialize NVS
    static NvsHandler nvs;

    // TODO: initialize WiFi
    static WifiHandler wifi(nvs);
    ESP_ERROR_CHECK(nvs.init());
    // TODO: wait for WiFi connection
    wifi.init();
    wifi.clearCreds();
    //if (!wifi.hasStoredCreds())
    //{
    //    wifi.saveCreds("Yeahno", "kelArotta");
    //}
    if (wifi.hasStoredCreds())
    {
        if (wifi.startStation())
        {
            ESP_LOGI(TAG, "Connected to wifi.");
        } else
        {
            ESP_LOGE(TAG, "Failed to connect to wifi with stored creds.");
        }
    }
    else
    {
        wifi.startSoftAP();
    }
    // create client.
    MQTTHandler mqtt;

    // create dummy data
    ntc_data ntc_d = {.temperature = 25};
    pulse_data pulse_d = {.heart_rate = 100, .spo2 = 50};
    combined_data data = {.ntc = ntc_d, .pulse = pulse_d, .ntc_valid = true, .pulse_valid = true};
    vTaskDelay(pdMS_TO_TICKS(3000));
    mqtt.publish(data);

    while (true) {
        ledState = !ledState;
        gpio_set_level(onboardLED, ledState);
        //ESP_LOGI(mainTag, "LED: %s\n", ledState ? "on" : "off");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
