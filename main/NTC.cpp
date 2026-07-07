#include "config.cpp"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>
static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t cali_handle;
void configure_adc() {
  adc_oneshot_unit_init_cfg_t init_cfg = {.unit_id = ADC_UNIT, .clk_src = ADC_DIGI_CLK_SRC_DEFAULT, .ulp_mode = ADC_ULP_MODE_DISABLE};
  adc_oneshot_new_unit(&init_cfg, &adc_handle);

  adc_oneshot_chan_cfg_t chan_cfg = {
      .atten = ADC_ATTEN_DB_12, // full 0-3.3V range (renamed from DB_11 in
                                // newer IDF)
      .bitwidth = ADC_BITWIDTH_DEFAULT

  };
  adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &chan_cfg);

  // Calibration so raw counts convert to real millivolts
  adc_cali_curve_fitting_config_t cali_cfg = {
      .unit_id = ADC_UNIT,
      .chan = ADC_CHANNEL,
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle);
}
extern "C" void app_main(void) {
  configure_adc();
  while (1) {
    int raw = 0, mv = 0;
    adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw);
    adc_cali_raw_to_voltage(cali_handle, raw, &mv);

    float vout = mv / 1000.0f;

    if (vout <= 0.01f || vout >= VIN - 0.01f) {
      printf("Reading out of range — check wiring\n");
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    // NTC on top, R_FIXED on bottom:
    float r_ntc = R_FIXED * ((VIN / vout) - 1.0f);
    // If wired the other way, use instead:
    // float r_ntc = R_FIXED * (vout / (VIN - vout));

    // Beta equation: 1/T = 1/T0 + (1/B) * ln(R/R0)
    float tempK = 1.0f / ((1.0f / T0_KELVIN) + (logf(r_ntc / R0) / BETA));
    float tempC = tempK - 273.15f;

    printf("Raw: %d  Vout: %.3fV  R_ntc: %.1f ohm  Temp: %.2f C\n", raw, vout,
           r_ntc, tempC);

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
