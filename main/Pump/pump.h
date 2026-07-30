#ifndef BLINK_PUMP_H
#define BLINK_PUMP_H
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>

#include "esp_log.h"
#include "soc/gpio_num.h"
#include "driver/gpio.h"
#include "atomic"
#define PUMP_IN1 GPIO_NUM_19
#define PUMP_IN2 GPIO_NUM_20

static const char* PUMP_TAG = "Pump";

class Pump {

public:
    Pump();

    void init();
    void on();
    void off();
    bool operator()() const;
private:
    enum class PumpCmd { OFF, ON };
    QueueHandle_t queue;
    TaskHandle_t task_handle = nullptr;

    static void task_wrap(void *pvParameters);

    void main_task();
    std::atomic<bool> state{false};
};


#endif //BLINK_PUMP_H
