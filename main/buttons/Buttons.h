// buttons/Buttons.h
#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// poll SW1/2, post EventType::Button into processing
// short press sent on release, if held for3+ secs, sends @ 3sec once but not on release.
class Buttons {
public:
    explicit Buttons(QueueHandle_t queue) : queue_(queue) {}
    esp_err_t begin();
    Buttons(const Buttons &) = delete;
    Buttons &operator=(const Buttons &) = delete;

private:
    static void taskEntry(void *arg);
    void run();
    void post(int index, bool long_press);

    QueueHandle_t queue_;
    TaskHandle_t  task_ = nullptr;
};