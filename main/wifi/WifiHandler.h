#pragma once
#include <string>
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "../nvs/NvsHandler.h"

class WifiHandler {
public:
    explicit WifiHandler(NvsHandler& nvs);

    void init();
    bool startStation();  // reads creds from NVS, blocks until connected/failed
    void startSoftAP(const char* ssid, const char* password);
    bool hasStoredCreds();
    esp_err_t saveCreds(const std::string& ssid, const std::string& password);

private:
    static void eventHandler(void* arg, esp_event_base_t base, int32_t id, void* data);

    NvsHandler& nvs_;
    EventGroupHandle_t eventGroup_ = nullptr;
    int retryCount_ = 0;

    static constexpr const char* NVS_NAMESPACE = "wifi";
    static constexpr int MAX_RETRY = 5;
};