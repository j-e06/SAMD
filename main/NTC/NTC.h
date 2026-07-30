
#ifndef BLINK_NTC_H
#define BLINK_NTC_H
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>

#include "esp_log.h"

#define ADC_UNIT ADC_UNIT_1 // default
#define ADC_CHANNEL ADC_CHANNEL_1 // pin 1
#define R_FIXED 10000.0f // TE GA10K3A1 pairing, other resistor in divider
#define R0 10000.0f  // NTC nominal resistance at 25C
#define BETA 3976.0f // NTC Beta 25/85
#define T0_KELVIN 298.15f
#define VIN 3.3f // what is supplied to other resistor
#define READ_FREQUENCY 1000 // how often to read, in MS

static const char* NTC_TAG = "NTC";

class Thermistor {
public:
    Thermistor(QueueHandle_t queue);
private:
    QueueHandle_t queue; // to send data to, to be parsed elsewhere
    adc_oneshot_unit_handle_t adc_handle;
    adc_cali_handle_t cali_handle;
    bool read_data = false;
    TaskHandle_t task_handle = nullptr;
    esp_err_t read(float *temp);

    void init();

    static void task_wrap(void *pvParameters);
    void reading_task();
};

#endif //BLINK_NTC_H
