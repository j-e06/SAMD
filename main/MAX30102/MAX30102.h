#pragma once
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// DFRobot SEN0518, DATASHEET: https://dfimg.dfrobot.com/wiki/19427/SEN0518_max30102-ppg-heart-rate-and-oximeter-sensor_datasheet_V1.0.pdf
// so, in hindsight this was horrible. it is extremely inaccurate (heart rate diff to reality sometimes +40 bpm
// a documented issue.
// can't do anything about it due to there being an mcu between the sensor itself and our entry
// so we can only really query the mcu for data that's already been calculated, which is done badly.
class MAX30102
{
public:
    MAX30102(QueueHandle_t queue, uint8_t address = 0x57)
        : queue_(queue), address_(address) {}
    MAX30102(const MAX30102 &) = delete;
    MAX30102 &operator=(const MAX30102 &) = delete;

    esp_err_t begin();   // adds the i2c device and starts the reading task

private:
    static void taskEntry(void* arg);
    void run();
    esp_err_t startCollect();
    esp_err_t readPulse(int32_t* heart_rate, int32_t* spo2);   // both come from one 8-byte read
    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t dev_ = nullptr;
    QueueHandle_t queue_;
    uint8_t address_;
    bool collecting_ = false;
};