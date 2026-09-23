#pragma once
#include <string>
#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "../nvs/NvsHandler.h"

class WifiHandler {
public:
    explicit WifiHandler(NvsHandler& nvs);
    ~WifiHandler();

    // Sets up netif/event loop/wifi driver and registers event handlers.
    // Safe to call more than once; startStation()/startSoftAP() call it
    // automatically if it hasn't run yet.
    esp_err_t init();

    bool operator()() const { return connected_; }

    // Reads creds from NVS, blocks (up to `timeout`) until connected or failed.
    // Defaults to the old behavior of blocking forever.
    esp_err_t startStation(TickType_t timeout = portMAX_DELAY);

    // password may be nullptr/empty for an open AP.
    esp_err_t startSoftAP(const char* ssid, const char* password);

    bool hasStoredCreds();
    esp_err_t saveCreds(const std::string& ssid, const std::string& password);

private:
    static void eventHandler(void* arg, esp_event_base_t base, int32_t id, void* data);

    NvsHandler& nvs_;
    EventGroupHandle_t eventGroup_ = nullptr;
    esp_event_handler_instance_t wifiEventInstance_ = nullptr;
    esp_event_handler_instance_t ipEventInstance_ = nullptr;
    int retryCount_ = 0;
    bool connected_ = false;
    bool initialized_ = false;

    static constexpr const char* NVS_NAMESPACE = "wifi";
    static constexpr int MAX_RETRY = 5;
    static constexpr size_t MAX_SSID_LEN = 32;
    static constexpr size_t MAX_PASS_LEN = 64;
};