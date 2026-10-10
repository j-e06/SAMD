#pragma once
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "utilities.h"   // ADC_UNIT, ADC_CHANNEL

class Thermistor {
public:
    explicit Thermistor(QueueHandle_t queue) : queue_(queue) {}
    Thermistor(const Thermistor &) = delete;
    Thermistor &operator=(const Thermistor &) = delete;

    // setup adc and start sampling
    esp_err_t begin();

private:
    esp_err_t init();
    esp_err_t read(float* temp);
    static void taskEntry(void* arg);
    void run();

    QueueHandle_t queue_;   // readings into processing via this que
    adc_oneshot_unit_handle_t adc_ = nullptr;
    adc_cali_handle_t cali_ = nullptr;
};