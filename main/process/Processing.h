//
// Created by janie on 04/08/2026.
//

#ifndef BLINK_PROCESSING_H
#define BLINK_PROCESSING_H

#include "structs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "Processing";

// define LED gpios
#define GREEN_LED GPIO_NUM_15 // placeholder gpios
#define AMBER_LED GPIO_NUM_16 // placeholder gpios
#define RED_LED GPIO_NUM_17 // placeholder gpios


class Processing {
public:
    Processing(QueueHandle_t queue);

private:
    QueueHandle_t queue;

    TaskHandle_t task_handle = nullptr;



    void init();

    static void task_wrap(void *pvParameters);
    void processing_task();

};


#endif //BLINK_PROCESSING_H
