#include "MAX30102.h"
#include <string.h>
#include <stdio.h>

MAX30102::MAX30102(QueueHandle_t queue, i2c_master_bus_handle_t bus_handle, uint8_t device_address, uint32_t scl_speed_hz)
    : spo2_hr_queue(queue), i2c_bus_handle(bus_handle), device_addr(device_address), scl_speed_hz(scl_speed_hz)
{
    init();
    xTaskCreate(&MAX30102::task_wrap, "MAX30102_task", 4096, this, 5, &task_handle);
}

void MAX30102::init()
{
    i2c_device_config_t max30102_dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = device_addr,
        .scl_speed_hz = scl_speed_hz,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0,
        },
    };

    esp_err_t dev_err = i2c_master_bus_add_device(i2c_bus_handle, &max30102_dev_config, &i2c_handle);
    if (dev_err != ESP_OK)
    {
        printf("Failed to add MAX30102 device: %s\n", esp_err_to_name(dev_err));
    }
    else
    {
        printf("MAX30102 device added successfully at 0x%02X\n", device_addr);
    }
}

void MAX30102::task_wrap(void *pvParameters)
{
    MAX30102 *self = static_cast<MAX30102 *>(pvParameters);
    self->the_task();
}

void MAX30102::the_task()
{
    while (true)
    {
        SensorReading reading; // struct holding both spo2 and heart_rate together
        if (read_pulse_and_spo(&reading.heart_rate, &reading.spo2) != ESP_OK)
        {
            // failed to read data, give it a bit more time to try again.
            vTaskDelay(pdMS_TO_TICKS(READ_FREQUENCY * 3));
            continue;
        }
        // successfully got data! send it as one combined item.
        xQueueSendToBack(spo2_hr_queue, &reading, portMAX_DELAY);

        vTaskDelay(pdMS_TO_TICKS(READ_FREQUENCY));
    }
}

// Write: [reg_addr][data...] as one transaction
// makes a buffer. Espidf allows us to use len for the size of a stack array.
// put the address in the first byte, then copy the data into the rest of the buffer, then send it all at once.
// i2c_master_transmit() will handle the STOP condition for us, so we don't need to worry about that.
// address -> ack -> data -> ack -> stop
esp_err_t MAX30102::write_reg(uint8_t reg_addr, uint8_t *data_buf, size_t len)
{
    uint8_t buf[len + 1];
    buf[0] = reg_addr;
    memcpy(&buf[1], data_buf, len);
    return i2c_master_transmit(i2c_handle, buf, len + 1, I2C_TIMEOUT_MS);
}

// we just send the register address, then read the data back.
esp_err_t MAX30102::read_reg(uint8_t reg_addr, uint8_t *data_buf, size_t len)
{
    esp_err_t err = i2c_master_transmit(i2c_handle, &reg_addr, 1, I2C_TIMEOUT_MS);
    if (err != ESP_OK)
        return err;
    return i2c_master_receive(i2c_handle, data_buf, len, I2C_TIMEOUT_MS);
}

// to start collecting, we write 0x00, 0x01 to the collect control register
esp_err_t MAX30102::start_collect()
{
    uint8_t data[2] = {0x00, 0x01};
    return write_reg(SEN0518_REG_COLLECT_CTRL, data, 2);
}

// to stop collecting, we write 0x00, 0x02 to the collect control register
esp_err_t MAX30102::stop_collect()
{
    uint8_t data[2] = {0x00, 0x02};
    return write_reg(SEN0518_REG_COLLECT_CTRL, data, 2);
}

esp_err_t MAX30102::read_pulse_and_spo(int32_t *heart_rate, int32_t *spo2)
{
    uint8_t rbuf[8];                                               // read buffer for 8 bytes of data from the sensor // raw data
    esp_err_t err = read_reg(SEN0518_REG_HEARTBEAT_SPO2, rbuf, 8); // reads and puts them to rbuf
    if (err != ESP_OK)
        return err;

    int32_t spo2_val = rbuf[0];
    if (spo2_val == 0)
        spo2_val = -1; // sensor reports 0 when no valid reading yet

    // the sensor sends the heart rate as a 32-bit signed integer in big-endian format, so we need to combine the bytes into a single integer.
    // the heart rate is in bytes 2-5 of the read buffer, so we need to shift them to combine
    // big endian, so the most significant byte is first, and the least significant byte is last.

    // going to leave the [1] as is, same done in DFRobot's Arduino library,
    // but it seems like it might be a status byte or something, since it's not used in the calculation of the heart rate.

    // could also be writen as:
    /*
    int32_t part1 = (int32_t)rbuf[2] << 24;   // calculation #1, alone
    int32_t part2 = (int32_t)rbuf[3] << 16;   // calculation #2, alone
    int32_t part3 = (int32_t)rbuf[4] << 8;    // calculation #3, alone
    int32_t part4 = (int32_t)rbuf[5];         // calculation #4, alone

    int32_t hr_val = part1 | part2 | part3 | part4;  // THEN combine

    */

    // ^ that code would be a lot easier to read, but the below is more modern/stylish way of writing
    int32_t hr_val = ((int32_t)rbuf[2] << 24) | ((int32_t)rbuf[3] << 16) |
                     ((int32_t)rbuf[4] << 8) | (int32_t)rbuf[5];
    if (hr_val == 0)
        hr_val = -1;

    if (heart_rate)
        *heart_rate = hr_val;
    if (spo2)
        *spo2 = spo2_val;
    return ESP_OK;
}