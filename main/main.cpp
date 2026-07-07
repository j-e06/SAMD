#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Thermistor.cpp"
#include "config.cpp"
extern "C" void app_main(void) {
    NTC ntc;

    float temp = ntc.getReading();

    printf("Temperature is %f\n", temp);


  while (true) {
        printf("Temp: %f\n", (ntc.getReading()));
        vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
