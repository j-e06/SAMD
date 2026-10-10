#pragma once
#include <atomic>
#include <string>
#include "esp_err.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "../nvs/NvsHandler.h"

class WifiHandler {
public:
    explicit WifiHandler(NvsHandler& nvs) : nvs_(nvs) {}

    esp_err_t init();

    // connect with stored creds, wait till timeout and keeptrying in background
    esp_err_t startStation(TickType_t timeout);

    // provisioning path, tries creds 5x then gives up, creds saved by caller
    esp_err_t tryStation(const std::string& ssid, const std::string& password, TickType_t timeout);

    // password may be nullptr or empty for an open AP.
    // password nullptr/empty = open AP
    esp_err_t startSoftAP(const char* ssid, const char* password);

    bool hasStoredCreds();
    esp_err_t saveCreds(const std::string& ssid, const std::string& password);
    esp_err_t clearCreds();
    WifiHandler(const WifiHandler &) = delete;
    WifiHandler &operator=(const WifiHandler &) = delete;

private:
    esp_err_t connect(const std::string& ssid, const std::string& password,
                      TickType_t timeout, bool persistent);
    static void eventHandler(void* arg, esp_event_base_t base, int32_t id, void* data);
    static void reconnectCb(void* arg);

    NvsHandler& nvs_;
    EventGroupHandle_t eventGroup_ = nullptr;
    esp_timer_handle_t reconnectTimer_ = nullptr;
    std::atomic<int> retryCount_{0};
    std::atomic<bool> everConnected_{false};
    std::atomic<bool> persistent_{false};
    bool initialized_ = false;

    static constexpr const char* NVS_NAMESPACE = "wifi";
    static constexpr int MAX_RETRY = 5;
    static constexpr size_t MAX_SSID_LEN = 32;
    static constexpr size_t MAX_PASS_LEN = 64;
};