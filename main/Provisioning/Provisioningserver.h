#pragma once
#include <atomic>
#include <string>
#include "esp_http_server.h"
#include "../wifi/WifiHandler.h"

class ProvisioningServer {
public:
    ProvisioningServer(NvsHandler& nvs, WifiHandler& wifi) : nvs_(nvs), wifi_(wifi) {}
    // connect calls this once creds have been tried & worked
    void onCredsSaved(void (*cb)(bool connected)) { credsSavedCb_ = cb; }

    esp_err_t start();
    ProvisioningServer(const ProvisioningServer &) = delete;
    ProvisioningServer &operator=(const ProvisioningServer &) = delete;
private:
    enum class ConnectStatus { Idle, Connecting, Connected, Failed };

    static esp_err_t rootGetHandler(httpd_req_t* req);
    static esp_err_t saveCredsPostHandler(httpd_req_t* req);
    static esp_err_t statusGetHandler(httpd_req_t* req);
    static void connectTaskEntry(void* arg);

    NvsHandler& nvs_;
    WifiHandler& wifi_;
    std::string pendingSsid_, pendingPass_, pendingBroker_;
    httpd_handle_t server_ = nullptr;
    void (*credsSavedCb_)(bool connected) = nullptr;
    std::atomic<ConnectStatus> status_{ConnectStatus::Idle};

    static constexpr TickType_t CONNECT_TIMEOUT = pdMS_TO_TICKS(30000);
};