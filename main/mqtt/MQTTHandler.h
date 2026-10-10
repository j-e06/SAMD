#pragma once

#include <atomic>
#include "esp_err.h"
#include "mqtt_client.h"
#include "utilities.h"

inline constexpr const char *DEFAULT_BROKER_URI = CONFIG_SAMD_BROKER_URI;
inline constexpr const char *DATA_TOPIC   = "sleep/data";     // info out
inline constexpr const char *CMD_TOPIC    = "sleep/cmd";      // commands in, start pause continue stop
inline constexpr const char *STATUS_TOPIC = "sleep/status";   // device state, "last will"

class MQTTHandler {
public:
    explicit MQTTHandler(QueueHandle_t cmd_queue) : cmdQueue_(cmd_queue) {}
    MQTTHandler(const MQTTHandler &) = delete;
    MQTTHandler &operator=(const MQTTHandler &) = delete;

    // creates the mqqtt client, auto attemps connection once wifi has ip
    esp_err_t begin(const char *brokerUri);

    bool isConnected() const { return connected_; }

    // attempt publishing when data ready, if broker can't be reached then oh well. data gets dropped
    bool publish(const combined_data &data);

    // state pointer gets republished after every reconnect,
    bool publishStatus(const char *state, const char *detail = nullptr);

private:
    void handleMqttEvent(esp_mqtt_event_handle_t event);
    static void mqttEventHandler(void *arg, esp_event_base_t base, int32_t id, void *data);

    esp_mqtt_client_handle_t client_ = nullptr;
    QueueHandle_t cmdQueue_;                    // processing queue; commands are posted here
    std::atomic<bool> connected_{false};        // broker session is up
    std::atomic<const char *> lastState_{nullptr};
};