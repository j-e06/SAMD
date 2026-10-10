#pragma once

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "sdkconfig.h"
#include <optional>
#include <stdint.h>

constexpr gpio_num_t GREEN_LED = GPIO_NUM_15;
constexpr gpio_num_t AMBER_LED = GPIO_NUM_16;
constexpr gpio_num_t RED_LED = GPIO_NUM_17;
constexpr gpio_num_t I2C_SDA = GPIO_NUM_19;
constexpr gpio_num_t I2C_SCL = GPIO_NUM_20;

constexpr gpio_num_t SW1_BTN = GPIO_NUM_21;
constexpr gpio_num_t SW2_BTN = GPIO_NUM_22;
constexpr gpio_num_t PUMP_IN1 = GPIO_NUM_4;
constexpr gpio_num_t PUMP_IN2 = GPIO_NUM_5;

constexpr adc_unit_t NTC_ADC_UNIT = ADC_UNIT_1;
constexpr adc_channel_t NTC_ADC_CHANNEL = ADC_CHANNEL_1; // GPIO1

inline constexpr const char *BROKER_NVS_NS = "mqtt";
inline constexpr const char *BROKER_NVS_KEY = "uri";
// given to mqtt last will
#define DEVICE_ID CONFIG_SAMD_DEVICE_ID

// what types of events are accepted into the processing task
enum class EventType : uint8_t { Ntc, Pulse, Command, Button };

// payloads of sleep/cmd
enum class Command : uint8_t { Start, Pause, Continue, Stop };

struct ntc_data {
  float temperature;
};
struct pulse_data {
  int32_t heart_rate;
  int32_t spo2;
}; // -1 = no valid reading
struct command_data {
  Command command;
};
struct button_data {
  uint8_t id;
  bool long_press;
}; // id 1 = SW1, 2 = SW2

// entry into the processing queue (copied by value)
// an entry of this type, stamped with the current time
struct queue_entry {
  EventType type;
  int64_t ts_us;
  union {
    ntc_data ntc;
    pulse_data pulse;
    command_data command;
    button_data button;
  };
};

// could've gone with "global" time, ended up not due to not wanting to deal with NTP sync,
inline queue_entry make_entry(EventType type) {
  queue_entry e{};
  e.type = type;
  e.ts_us = esp_timer_get_time();
  return e;
}

struct combined_data {
  std::optional<float> temp;
  std::optional<int32_t> hr;
  std::optional<int32_t> spo2;
};

// non blocking post into queue, drops the event if queue is full
inline bool post_event(QueueHandle_t q, const queue_entry &e) {
  return xQueueSend(q, &e, 0) == pdTRUE;
}
