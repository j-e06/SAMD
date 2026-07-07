#include "esp_log.h"


#define CONFIG_ADC_UNIT ADC_UNIT_1
#define CONFIG_ADC_CHANNEL ADC_CHANNEL_2 // GPIO2 on ESP32-C6 (its only ADC unit)
#define CONFIG_ADC_R_FIXED 10000.0f // TE GA10K3A1 pairing, ohms — matches NTC nominal
#define CONFIG_ADC_R0 10000.0f  // NTC nominal resistance at 25C, ohms (GA10K3A1 datasheet)
#define CONFIG_ADC_BETA 3976.0f // NTC Beta 25/85, from GA10K3A1 datasheet
#define CONFIG_ADC_T0_KELVIN 298.15f
#define CONFIG_ADC_VIN 3.3f

static const char *NTC_TAG = "NTC";

static const char *WIFI_TAG = "wifi softAP";


#define CONFIG_SOFTAP_DEFAULT_SSID "esp32test"
#define CONFIG_SOFTAP_DEFAULT_PASSWORD "kelarotta"
#define CONFIG_SOFTAP_ESP_WIFI_CHANNEL 1
#define CONFIG_SOFTAP_MAX_STA_CONN 4