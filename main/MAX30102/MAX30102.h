#ifndef MAX30102_H
#define MAX30102_H
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// DFRobot Gravity SEN0518 on-board MCU protocol (NOT raw MAX30102 registers)
#define SEN0518_REG_HEARTBEAT_SPO2 0x0C // read 8 bytes
#define SEN0518_REG_TEMPERATURE    0x14 // read 2 bytes
#define SEN0518_REG_COLLECT_CTRL   0x20 // write 2 bytes: {0,1}=start {0,2}=stop

#define I2C_TIMEOUT_MS   100  // ms to wait on each I2C transaction before giving up
#define READ_FREQUENCY   1000 // ms between sensor reads in the_task()

// One combined sensor reading, sent through the queue as a single item.
struct SensorReading
{
    int32_t heart_rate;
    int32_t spo2;
};

class MAX30102
{
public:
    MAX30102(QueueHandle_t spo2_hr_queue, i2c_master_bus_handle_t bus_handle, uint8_t device_address = 0x57, uint32_t scl_speed_hz = 100000);

    void init();
    esp_err_t start_collect();
    esp_err_t stop_collect();
    

private:
    // FreeRTOS can only call a plain function pointer, not a member function directly.
    // task_wrap is that plain function: it receives `this` through pvParameters,
    // casts it back, and calls the_task() on it.
    static void task_wrap(void *pvParameters);
    void the_task();
    esp_err_t read_pulse_and_spo(int32_t *heart_rate, int32_t *spo2); // both come from the same 8-byte read

    esp_err_t write_reg(uint8_t reg_addr, uint8_t *data_buf, size_t len);
    esp_err_t read_reg(uint8_t reg_addr, uint8_t *data_buf, size_t len);

    i2c_master_dev_handle_t i2c_handle;
    i2c_master_bus_handle_t i2c_bus_handle;
    uint8_t device_addr;
    uint32_t scl_speed_hz;

    QueueHandle_t spo2_hr_queue;
    TaskHandle_t task_handle;
};
#endif // MAX30102_H