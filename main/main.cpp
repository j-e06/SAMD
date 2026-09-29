#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt/MQTTHandler.h"
#include "structs.h"
#include "nvs/NvsHandler.h"
#include "wifi/WifiHandler.h"
#include "Provisioning/Provisioningserver.h"
#define onboardLED GPIO_NUM_15
#define eraseWifi GPIO_NUM_16
static const char *mainTag = "MAIN";

extern "C" void app_main(void) {
    ESP_LOGI(mainTag, "Starting main.");

    bool ledState = false;

    gpio_reset_pin(onboardLED);
    gpio_set_direction(onboardLED, GPIO_MODE_OUTPUT);

    gpio_reset_pin(eraseWifi);
    gpio_set_direction(eraseWifi, GPIO_MODE_INPUT);
    gpio_set_pull_mode(eraseWifi, GPIO_PULLUP_ONLY);
    bool prevState = false;


    // initialize NVS
    static NvsHandler nvs;
    ESP_ERROR_CHECK(nvs.init());

    // initialize WiFi
    static WifiHandler wifi(nvs);
    ESP_ERROR_CHECK(wifi.init());
    if (!wifi.hasStoredCreds())
    {
        wifi.startSoftAP("esp32test", "fuckme123");
        static ProvisioningServer provServer(wifi);
        provServer.onCredsSaved([](bool connected) {
        if (connected) esp_restart();
        // on failure: do nothing, server stays up on the failure page for a retry
        });
        esp_err_t err = provServer.start();
        if (err != ESP_OK)
        {
            ESP_LOGE(mainTag, "Failed to start provisioning server: %s", esp_err_to_name(err));
        }
    }
    else{
        esp_err_t err = wifi.startStation();
        if (err == ESP_OK)
        {
            ESP_LOGI(mainTag, "Connected to wifi.");
        }
        else
        {
            nvs.eraseNamespace("wifi");
            ESP_LOGE(mainTag, "Failed to connect to wifi with stored creds: %s", esp_err_to_name(err));
        }
    }

    if (wifi())
    {
        // if we're connected we can run the mqtt testing.
        // create client.
        MQTTHandler mqtt;

        // create dummy data
        ntc_data ntc_d = {.temperature = 25};
        pulse_data pulse_d = {.heart_rate = 100, .spo2 = 50};
        combined_data data = {.ntc = ntc_d, .pulse = pulse_d, .ntc_valid = true, .pulse_valid = true};
        vTaskDelay(pdMS_TO_TICKS(3000));
        mqtt.publish(data);
    }

    // run the main loop regardless of mqtt state.
    while (true) {

        int curState = !gpio_get_level(eraseWifi);
        if (curState && !prevState)
        {
            nvs.eraseNamespace("wifi");
            ESP_LOGI(mainTag, "Namespace wifi erased.");
            esp_restart();
        }
        prevState = curState;

        ledState = !ledState;
        gpio_set_level(onboardLED, ledState);
        //ESP_LOGI(mainTag, "LED: %s", ledState ? "on" : "off");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}