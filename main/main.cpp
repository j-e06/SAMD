#include "blink"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>
void app_main(void) {

  // create queue
  //
  QueueHandle_t ntc_queue = xQueueCreate(10, sizeof(float));

  NTC ntc(&ntc_queue);

  vTaskStartScheduler()
}
