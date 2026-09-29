#pragma once
#include <atomic>
#include <functional>
#include "esp_err.h"
#include "esp_http_server.h"
#include "../wifi/WifiHandler.h"

class ProvisioningServer {
public:
    explicit ProvisioningServer(WifiHandler& wifi);
    ~ProvisioningServer();

    void onCredsSaved(std::function<void(bool connected)> cb) { credsSavedCb_ = std::move(cb); }

    esp_err_t start();
    esp_err_t stop();

private:
    enum class ConnectStatus { Idle, Connecting, Connected, Failed };

    static esp_err_t rootGetHandler(httpd_req_t* req);
    static esp_err_t saveCredsPostHandler(httpd_req_t* req);
    static esp_err_t statusGetHandler(httpd_req_t* req);
    static void connectTaskEntry(void* arg);

    esp_err_t handleSaveCreds(httpd_req_t* req);
    esp_err_t registerHandler(const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t*));

    WifiHandler& wifi_;
    httpd_handle_t server_ = nullptr;
    std::function<void(bool connected)> credsSavedCb_;
    std::atomic<ConnectStatus> status_{ConnectStatus::Idle};

    static constexpr size_t MAX_BODY_LEN = 512;
    static constexpr TickType_t CONNECT_TIMEOUT = pdMS_TO_TICKS(15000);
};