#include "NTC.h"



Thermistor::Thermistor(QueueHandle_t queue): queue(queue){
    init();
    xTaskCreate(&Thermistor::task_wrap, "NTC_task", 4096, this, 5, &task_handle);
}

void Thermistor::init() {
    ESP_LOGI(NTC_TAG, "Start NTC init.\n");
    // start INIT of given ADC unit.

    // create the one shot cfg
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
        .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE};
    adc_oneshot_new_unit(&init_cfg, &adc_handle);

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12, // works for 0-3.3V range
        .bitwidth = ADC_BITWIDTH_DEFAULT
    };

    adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &chan_cfg);

    // calibration for raw counts to millivolts

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT,
        .chan =  ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT
    };

    adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle);

    ESP_LOGI(NTC_TAG, "Init complete.\n");

}
void Thermistor::task_wrap(void *pvParameters) {
    static_cast<Thermistor*>(pvParameters)->reading_task();
}

void Thermistor::reading_task() {
    while (true) {
        float temp;
        if (read(&temp) != ESP_OK) {
            // failed to read data, give it a bit more time to try again.
            vTaskDelay(pdMS_TO_TICKS(READ_FREQUENCY * 3));
            continue;
        }
        // successfully got data!
        // send it to queue.
        xQueueSendToBack(queue, &temp, portMAX_DELAY);

        vTaskDelay(pdMS_TO_TICKS(READ_FREQUENCY));

    }
}

esp_err_t Thermistor::read(float *temp) {
    int raw = 0, mv = 0;

    adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw);
    adc_cali_raw_to_voltage(cali_handle, raw, &mv);

    float vout = mv / 1000.0f;

    // check reading is valid
    if (vout <= 0.01f || vout >= VIN - 0.01f) {
        ESP_LOGE(NTC_TAG, "Failed to read data out of NTC, voltage out of range: %f", vout);
        return ESP_FAIL;
    }

    float r_ntc = R_FIXED * ((VIN/vout) - 1.0f);

    *temp = (1.0f / ((1.0f / T0_KELVIN) + (logf(r_ntc / R0) / BETA))) - 273.15f;
    return ESP_OK;
}
