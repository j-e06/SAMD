//
// Created by janie on 04/08/2026.
//

#include "Processing.h"


Processing::Processing(QueueHandle_t queue): queue(queue) {
    init();
    xTaskCreate(&Processing::task_wrap, "Processing Task", 4096, this, 5, &task_handle);
}


void Processing::init() {
    // init LEDS
    gpio_reset_pin(GREEN_LED);
    gpio_set_direction(GREEN_LED, GPIO_MODE_OUTPUT);
    gpio_set_level(GREEN_LED, 0);

    gpio_reset_pin(AMBER_LED);
    gpio_set_direction(AMBER_LED, GPIO_MODE_OUTPUT);
    gpio_set_level(AMBER_LED, 0);

    gpio_reset_pin(RED_LED);
    gpio_set_direction(RED_LED, GPIO_MODE_OUTPUT);
    gpio_set_level(RED_LED, 0);
}

void Processing::task_wrap(void *pvParameters) {
    static_cast<Processing*>(pvParameters)->processing_task();
}

void Processing::processing_task() {
    ESP_LOGI(TAG, "Processing task started.");

    queue_entry *entry = nullptr;

    combined_data combined{};

    while (true) {
        if (xQueueReceive(queue, &entry, portMAX_DELAY) == pdTRUE) {
            // we got new data / shit to do!
            switch (entry->type) {
                case NTC:
                    combined.ntc = entry->ntc;
                    combined.ntc_valid = true;
                    ESP_LOGI(TAG, "NTC data received with value %f.", entry->ntc.temperature);
                    break;
                case PULSE:
                    combined.pulse = entry->pulse;
                    combined.pulse_valid = true;
                    ESP_LOGI(TAG, "Pulse data received with value hr %f | spo2 %f.", entry->pulse.heart_rate, entry->pulse.spo2);
                    break;
                case COMMAND:
                    ESP_LOGI(TAG, "Command received with value: %u", entry->command);
                    // execute command, likely just on/off of device or pump on/off
                    // do later once listener task done?
                    break;
                default:
                    ESP_LOGE(TAG, "Processing task switch case default reached, should be impossible. %s", entry->type);
                    break;
            }

            // check if we have all the data.
            if (combined.ntc_valid && combined.pulse_valid) {
                // we have all data, so lets send with current time since boot.


                // data sent succesfully, "reset" state.
                combined.ntc_valid = false;
                combined.pulse_valid = false;
            }
        }
    }

}
