#pragma once
#include "esp_err.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "utilities.h"
// h bridge pump, in1 high we run, in2 held low.
// since this is a pretty bad pump, I decided to add a failsafe timer so the thing doesn't run 24/7
// even if we don't get an off command, it'll stop after enough time just in case.

class Pump {
public:
    Pump();
    Pump(const Pump &) = delete;
    Pump &operator=(const Pump &) = delete;
    // esp_err_t to make sure pump stays off even if failsafe is unavailable
    esp_err_t on();
    void off();

private:
    static void failsafeCb(void *arg);
    esp_timer_handle_t failsafe_ = nullptr;
};