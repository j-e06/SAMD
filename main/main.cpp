#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "NTC/NTC.h"

static const char* MAIN_TAG = "main";

extern "C" void app_main(void) {

    QueueHandle_t NTC_queue = xQueueCreate(10, sizeof(float));

    Thermistor NTC(NTC_queue);

    while (true) {
        float temp;
        if (xQueueReceive(NTC_queue, &temp, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(MAIN_TAG, "Received temp data: %f\n", temp);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
