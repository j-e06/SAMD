//
// Created by janie on 04/08/2026.
//

#ifndef BLINK_MQTTHANDLER_H
#define BLINK_MQTTHANDLER_H

#include "mqtt_client.h"
#include "esp_log.h"

#include "structs.h"
static const char *TAG = "MQTT Handler";
inline const char* BROKER_URI = "mqtt://192.168.1.135:1883";
#define BROKER_TOPIC "sleep/data"

class MQTTHandler {
public:
    MQTTHandler();
    bool publish(combined_data data);
private:
    bool connected = false;

    esp_mqtt_client_handle_t client = nullptr;

    void init();
    static void event_handler(
        void *handler_args,
        esp_event_base_t base,
        int32_t event_id,
        void *event_data
        );
    void handleEvent(esp_mqtt_event_handle_t event);
};


#endif //BLINK_MQTTHANDLER_H
