//
// Created by janie on 04/08/2026.
//

#include "MQTTHandler.h"

#include <time.h>

#include "esp_timer.h"

MQTTHandler::MQTTHandler() {
    init();
}

void MQTTHandler::init() {
    esp_mqtt_client_config_t config = {};

    config.broker.address.hostname = BROKER_URL;
    config.broker.address.port = BROKER_PORT;

    client = esp_mqtt_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize MQTT Client");
        return;
    }
    esp_mqtt_client_register_event(
        client,
        MQTT_EVENT_ANY,
        event_handler,
        this);

    esp_err_t msg_id = esp_mqtt_client_start(client);
    if (msg_id != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %d", msg_id);
        return;
    }
    // subscribe to topic.
    int id = esp_mqtt_client_subscribe(client, BROKER_TOPIC, 1);
    if (id < 0) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %d", msg_id);
        return;

    }
}

void MQTTHandler::event_handler(
    void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data)
{
    auto *self = static_cast<MQTTHandler *>(handler_args);
    auto event = static_cast<esp_mqtt_event_handle_t>(event_data);
    self->handleEvent(event);

}

void MQTTHandler::handleEvent(esp_mqtt_event_handle_t event) {
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            connected = true;
            ESP_LOGI(TAG, "MQTT Connected");
            break;
        case MQTT_EVENT_DISCONNECTED:
            connected = false;
            ESP_LOGI(TAG, "MQTT Disconnected");
            break;
        case MQTT_EVENT_DATA:
            // data received
            ESP_LOGI(TAG, "MQTT Data");
            break;
        default:
            ESP_LOGI(TAG, "Unhandled MQTT Event: %d", event->event_id);
            break;

    }
}

void MQTTHandler::publish(combined_data data) {
    if (!connected) {
        ESP_LOGE(TAG, "Tried to publish without MQTT connection.");
    }
    uint64_t now = esp_timer_get_time() / 1000; // time since boot in ms
    char payload[128];

    snprintf(payload,
        sizeof(payload),
        "{\"timestamp\":%lld,\"hr\":%.1ld,\"spo\":%ld,\"temp\":%f}",
        (long long)now,
        data.pulse.heart_rate,
        data.pulse.spo2,
        data.ntc.temperature
        );
    int msg_id = esp_mqtt_client_publish(
        client,
        BROKER_TOPIC,
        payload,
        0,
        1,
        0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish message, msg id: %d", msg_id);
    }
}
