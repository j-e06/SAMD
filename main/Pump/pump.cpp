#include "pump.h"



Pump::Pump() {
    init();
    queue = xQueueCreate(5, sizeof(PumpCmd));
    xTaskCreate(&Pump::task_wrap, "Pump", 2048, this, 5, &task_handle);
}

void Pump::init() {
    gpio_reset_pin(PUMP_IN1);
    gpio_reset_pin(PUMP_IN2);

    gpio_set_direction(PUMP_IN1, GPIO_MODE_OUTPUT);
    gpio_set_direction(PUMP_IN2, GPIO_MODE_OUTPUT);

    // starts off.
    gpio_set_level(PUMP_IN1, 0);
    gpio_set_level(PUMP_IN2, 0);
}

bool Pump::operator()() const {
    return state.load();
}

void Pump::task_wrap(void *pvParameters) {
    static_cast<Pump*>(pvParameters)->main_task();
}

void Pump::on() {
    PumpCmd cmd = PumpCmd::ON;
    xQueueSend(queue, &cmd, portMAX_DELAY);
}
void Pump::off() {
    PumpCmd cmd = PumpCmd::OFF;
    xQueueSend(queue, &cmd, portMAX_DELAY);
}
void Pump::main_task() {
    ESP_LOGI(PUMP_TAG, "Starting Pump task.");
    PumpCmd cmd;
    while (1) {
        if (xQueueReceive(queue, &cmd, portMAX_DELAY) == pdTRUE) {
            if (cmd == PumpCmd::ON) {
                gpio_set_level(PUMP_IN1, 1);
                gpio_set_level(PUMP_IN2, 0);
                state.store(true);
            } else {
                gpio_set_level(PUMP_IN1, 0);
                gpio_set_level(PUMP_IN2, 0);
                state.store(false);

            }
        }
    }

}
