#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "MAX30102/MAX30102.h"

static const char *mainTag = "MAIN";

extern "C" void app_main(void)
{
    ESP_LOGI(mainTag, "Starting main.\n");

    i2c_master_bus_config_t i2c_bus_config = {
        // making a struct with the configuration for the I2C bus
        .i2c_port = I2C_NUM_0,             // using I2C port 0 "controllers"
        .sda_io_num = GPIO_NUM_19,         // max D/T
        .scl_io_num = GPIO_NUM_20,         // max R/C
        .clk_source = I2C_CLK_SRC_DEFAULT, // using the default clock source for the I2C bus
        // setting the glitch ignore count to 7, esp default is 7, but can be set to 0-15, this is the number of clock cycles that the I2C bus will ignore glitches on the line
        .glitch_ignore_cnt = 7,
        .intr_priority = 1,     // setting the interrupt priority to 1, this is the highest priority for the I2C bus
        .trans_queue_depth = 0, // setting the transfer queue depth to 0, this means that the I2C bus will not queue any transfers, and will only process one transfer at a time
        .flags = {
            .enable_internal_pullup = 1, // enabling the internal pull-up resistors for the I2C bus, this is required for the I2C bus to function properly
            .allow_pd = 0,               // not allowing the I2C bus to enter power down mode, this is required for the I2C bus to function properly
        },
    };
    // queue for the SPO2 and HR data, this is used to send the data from the MAX30102 task to the main task
    QueueHandle_t SPO_HR_QUEUE = xQueueCreate(10, sizeof(SensorReading));
    if (SPO_HR_QUEUE == NULL)
    {
        printf("Failed to create SPO2/HR queue\n");
    }
    // creating a handle for the I2C bus, this is used to communicate with the I2C bus
    i2c_master_bus_handle_t i2c_bus_handle;

    // initializing the I2C bus with the configuration and handle, this will initialize the I2C bus and make it ready for communication
    esp_err_t err = i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle);
    if (err != ESP_OK)
    {
        printf("Failed to init I2C bus: %s\n", esp_err_to_name(err));
    }


    MAX30102 max30102(SPO_HR_QUEUE, i2c_bus_handle, 0x57); // creating an instance of the MAX30102

    vTaskDelay(pdMS_TO_TICKS(1000)); // waiting for 1 second to allow the MAX30102 sensor to collect data



    vTaskDelete(NULL);
}
