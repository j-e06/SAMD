#define ADC_UNIT ADC_UNIT_1
#define ADC_CHANNEL ADC_CHANNEL_2 // GPIO2 on ESP32-C6 (its only ADC unit)
#define R_FIXED 10000.0f // TE GA10K3A1 pairing, ohms — matches NTC nominal
#define R0 10000.0f  // NTC nominal resistance at 25C, ohms (GA10K3A1 datasheet)
#define BETA 3976.0f // NTC Beta 25/85, from GA10K3A1 datasheet
#define T0_KELVIN 298.15f
#define VIN 3.3f
