#include "NTC.h"
#include <math.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "NTC";
// NTC used TE GA10K3A1, in hindsight, bad choice due to the inaccuracy.
// DATASHEET: https://www.farnell.com/datasheets/2917194.pdf

static constexpr float R_FIXED = 10000.0f;   // TE GA10K3A1 pairing, the other resistor in the divider
static constexpr float R0 = 10000.0f;
static constexpr float BETA = 3976.0f;
static constexpr float T0_KELVIN = 298.15f;
static constexpr float VIN = 3.3f;           // given to NTC, ground to 10k resistor
static constexpr int NTC_READ_MS = 1000;
static constexpr int NTC_SAMPLES = 32;       // conversions averaged per reading
esp_err_t Thermistor::begin()
{
    ESP_RETURN_ON_ERROR(init(), TAG, "init");
    ESP_RETURN_ON_FALSE(xTaskCreate(&Thermistor::taskEntry, "NTC_task", 4096, this, 5, nullptr) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "task create");
    return ESP_OK;
}

void Thermistor::run()
{
    for (;;)
    {
        float temp;
        if (read(&temp) != ESP_OK)
        {
            vTaskDelay(pdMS_TO_TICKS(NTC_READ_MS * 3));   // give it a bit more time before the next try
            continue;
        }

        auto e = make_entry(EventType::Ntc);
        e.ntc.temperature = temp;
        post_event(queue_, e);

        vTaskDelay(pdMS_TO_TICKS(NTC_READ_MS));
    }
}
esp_err_t Thermistor::init()
{
    const adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = NTC_ADC_UNIT,
        .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,   // 0 - 3.3 V
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    // calibration turns raw counts into millivolts
    const adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = NTC_ADC_UNIT,
        .chan = NTC_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg, &adc_), TAG, "adc unit");
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(adc_, NTC_ADC_CHANNEL, &chan_cfg), TAG, "adc channel");
    ESP_RETURN_ON_ERROR(adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_), TAG, "adc calibration");
    return ESP_OK;
}

void Thermistor::taskEntry(void* arg)
{
    static_cast<Thermistor*>(arg)->run();
}
esp_err_t Thermistor::read(float* temp)
{
    // so. this is the hard part in some ways. the ntc is noisy as a concert, and that would 100%
    // cook the actual change in temperature we should be able to observer
    // so we have to get the average of multiple samples and calibrate based on that
    int rawSum = 0;
    for (int i = 0; i < NTC_SAMPLES; i++)
    {
        int raw = 0;
        ESP_RETURN_ON_ERROR(adc_oneshot_read(adc_, NTC_ADC_CHANNEL, &raw), TAG, "adc read");
        rawSum += raw;
    }
    int mv = 0;
    ESP_RETURN_ON_ERROR(adc_cali_raw_to_voltage(cali_, rawSum / NTC_SAMPLES, &mv), TAG, "adc convert");
    const float vout = mv / 1000.0f;

    // a floating or shorted input sits at the rails
    if (vout <= 0.01f || vout >= VIN - 0.01f)
    {
        ESP_LOGE(TAG, "voltage out of range: %.3f V", vout);
        return ESP_FAIL;
    }

    const float r_ntc = R_FIXED * (VIN / vout - 1.0f);
    const float t = 1.0f / (1.0f / T0_KELVIN + logf(r_ntc / R0) / BETA) - 273.15f;
    if (!(t > -40.0f && t < 125.0f))   // also rejects NaN
    {
        ESP_LOGE(TAG, "implausible temperature %.1f", t);
        return ESP_FAIL;
    }
    *temp = t;
    return ESP_OK;
}