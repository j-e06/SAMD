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

    config.broker.address.uri = BROKER_URI;

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

        case MQTT_EVENT_CONNECTED: {
            connected = true;
            ESP_LOGI(TAG, "MQTT Connected");

            int msg_id = esp_mqtt_client_subscribe_single(
                client,
                BROKER_TOPIC,
                1
            );

            if (msg_id < 0) {
                ESP_LOGE(TAG, "Failed to subscribe to '%s'", BROKER_TOPIC);
            } else {
                ESP_LOGI(TAG, "Subscribed to '%s', msg_id=%d",
                         BROKER_TOPIC, msg_id);
            }

            break;
        }

        case MQTT_EVENT_DISCONNECTED:
            connected = false;
            ESP_LOGI(TAG, "MQTT Disconnected");
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "Message published, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "Subscription acknowledged, msg_id=%d",
                     event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "Unsubscribed, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG,
                     "MQTT Data: topic='%.*s', data='%.*s'",
                     event->topic_len,
                     event->topic,
                     event->data_len,
                     event->data);
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT Error");

            if (event->error_handle != nullptr) {
                ESP_LOGE(TAG, "error_type=%d",
                         event->error_handle->error_type);

                ESP_LOGE(TAG, "tls_last_esp_err=0x%x",
                         event->error_handle->esp_tls_last_esp_err);

                ESP_LOGE(TAG, "tls_stack_err=0x%x",
                         event->error_handle->esp_tls_stack_err);

                ESP_LOGE(TAG, "transport_sock_errno=%d",
                         event->error_handle->esp_transport_sock_errno);
            }

            break;

        case MQTT_EVENT_BEFORE_CONNECT:
            ESP_LOGI(TAG, "MQTT Connecting...");
            break;

        case MQTT_EVENT_DELETED:
            connected = false;
            ESP_LOGI(TAG, "MQTT Client deleted");
            break;

        default:
            ESP_LOGI(TAG, "Unhandled MQTT event: %d",
                     event->event_id);
            break;
    }
}
bool MQTTHandler::publish(combined_data data) {
    if (!connected) {
        ESP_LOGE(TAG, "Tried to publish without MQTT connection.");
        return false;;
    }
    uint64_t now = esp_timer_get_time() / 1000; // time since boot in ms
    char payload[128];

    snprintf(payload,
        sizeof(payload),
        "{\"timestamp\":%lld,\"hr\":%.1ld,\"spo2\":%ld,\"temp\":%f}",
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
        return false;
    }
    else
    {
        ESP_LOGI(TAG, "Publish message: %d", msg_id);
        return true;
    }
}
