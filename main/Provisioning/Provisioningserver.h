#pragma once
#include <functional>
#include "esp_err.h"
#include "esp_http_server.h"

#include "../wifi/WifiHandler.h"

// Serves a small HTML form over the AP started by WifiHandler::startSoftAP(),
// and saves whatever the user submits via WifiHandler::saveCreds().
class ProvisioningServer {
public:
    explicit ProvisioningServer(WifiHandler& wifi);
    ~ProvisioningServer();

    // Called once creds have been saved successfully. Typical use:
    // esp_restart(), or stop() this server and call wifi.startStation().
    void onCredsSaved(std::function<void()> cb) { credsSavedCb_ = std::move(cb); }

    esp_err_t start();
    esp_err_t stop();

private:
    static esp_err_t rootGetHandler(httpd_req_t* req);
    static esp_err_t saveCredsPostHandler(httpd_req_t* req);
    esp_err_t handleSaveCreds(httpd_req_t* req);
    WifiHandler& wifi_;
    httpd_handle_t server_ = nullptr;
    std::function<void()> credsSavedCb_;

    static constexpr size_t MAX_BODY_LEN = 512;
};