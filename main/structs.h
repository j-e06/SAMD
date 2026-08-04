//
// Created by janie on 04/08/2026.
//

#ifndef BLINK_STRUCTS_H
#define BLINK_STRUCTS_H

#include "freertos/FreeRTOS.h"

// what types of events are accepted into processing task
enum EVENT_TYPE {
    NTC,
    PULSE,
    COMMAND
};

// ntc event data
struct ntc_data {
    float temperature;
};
// pulse event data
struct pulse_data {
    int32_t heart_rate;
    int32_t spo2;
};

// command event data
struct command_data {
    int32_t command;
};
// actual entry into que,
struct queue_entry{
    EVENT_TYPE type;
    union {
        ntc_data ntc;
        pulse_data pulse;
        command_data command;
    };
} ;

struct combined_data {
    ntc_data ntc;
    pulse_data pulse;

    bool ntc_valid;
    bool pulse_valid;
};
#endif //BLINK_STRUCTS_H
