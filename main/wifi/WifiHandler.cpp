#include "WifiHandler.h"
#include <algorithm>
#include <cstring>
#include <nvs.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_wifi.h"

static const char* TAG = "WifiHandler";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

esp_err_t WifiHandler::init()
{
    if (initialized_) return ESP_OK;

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    esp_err_t err = esp_event_loop_create_default();
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "event loop");  // already created is fine
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "set storage");

    esp_timer_create_args_t timerArgs = {};
    timerArgs.callback = &WifiHandler::reconnectCb;
    timerArgs.arg = this;
    timerArgs.name = "wifi_reconnect";
    ESP_RETURN_ON_ERROR(esp_timer_create(&timerArgs, &reconnectTimer_), TAG, "reconnect timer");

    eventGroup_ = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(eventGroup_, ESP_ERR_NO_MEM, TAG, "event group");

    ESP_RETURN_ON_ERROR(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiHandler::eventHandler, this), TAG, "wifi event register");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiHandler::eventHandler, this), TAG, "ip event register");

    initialized_ = true;
    ESP_LOGI(TAG, "Init complete.");
    return ESP_OK;
}

bool WifiHandler::hasStoredCreds()
{
    std::string ssid;
    return nvs_.getString(NVS_NAMESPACE, "ssid", ssid) == ESP_OK && !ssid.empty();
}

esp_err_t WifiHandler::saveCreds(const std::string& ssid, const std::string& password)
{
    ESP_RETURN_ON_FALSE(!ssid.empty() && ssid.size() <= MAX_SSID_LEN, ESP_ERR_INVALID_ARG, TAG, "ssid length");
    ESP_RETURN_ON_FALSE(password.size() <= MAX_PASS_LEN, ESP_ERR_INVALID_ARG, TAG, "password length");

    ESP_RETURN_ON_ERROR(nvs_.setString(NVS_NAMESPACE, "password", password), TAG, "save password");
    // ssid goes last so if power gets cut off between saving password and ssid, we get no creds vs only having ssid and no pass
    return nvs_.setString(NVS_NAMESPACE, "ssid", ssid);
}

esp_err_t WifiHandler::clearCreds()
{
    return nvs_.eraseNamespace(NVS_NAMESPACE);
}

void WifiHandler::reconnectCb(void* arg)
{
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN)
    {
        ESP_LOGW(TAG, "reconnect failed to start: %s", esp_err_to_name(err));
    }
}

esp_err_t WifiHandler::startStation(TickType_t timeout)
{
    ESP_RETURN_ON_ERROR(init(), TAG, "init");

    std::string ssid, password;
    if (nvs_.getString(NVS_NAMESPACE, "ssid", ssid) != ESP_OK ||
        nvs_.getString(NVS_NAMESPACE, "password", password) != ESP_OK)
    {
        ESP_LOGE(TAG, "No stored creds.");
        return ESP_ERR_NVS_NOT_FOUND;
    }
    return connect(ssid, password, timeout, true);
}

esp_err_t WifiHandler::tryStation(const std::string& ssid, const std::string& password, TickType_t timeout)
{
    ESP_RETURN_ON_ERROR(init(), TAG, "init");
    return connect(ssid, password, timeout, false);
}

esp_err_t WifiHandler::connect(const std::string& ssid, const std::string& password,
                               TickType_t timeout, bool persistent)
{
    ESP_RETURN_ON_FALSE(!ssid.empty() && ssid.size() <= MAX_SSID_LEN && password.size() <= MAX_PASS_LEN,
                        ESP_ERR_INVALID_SIZE, TAG, "creds do not fit wifi_config_t");

    // during provisioning softap NEEDS to stay up, even while we test creds
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);
    const bool apActive = !persistent && (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA);

    wifi_config_t wifiConfig = {};
    std::memcpy(wifiConfig.sta.ssid, ssid.c_str(), ssid.size());
    std::memcpy(wifiConfig.sta.password, password.c_str(), password.size());
    wifiConfig.sta.threshold.authmode = password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    // forget the previous attempt so we dont act on a stale result
    persistent_ = persistent;
    everConnected_ = false;
    retryCount_ = 0;
    esp_timer_stop(reconnectTimer_);   // errors if not running which si fine
    xEventGroupClearBits(eventGroup_, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(apActive ? WIFI_MODE_APSTA : WIFI_MODE_STA), TAG, "set_mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifiConfig), TAG, "set_config");
    esp_err_t err = esp_wifi_start();
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_WIFI_CONN, err, TAG, "wifi start");
    err = esp_wifi_connect();
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_WIFI_CONN, err, TAG, "wifi connect");

    EventBits_t bits = xEventGroupWaitBits(
        eventGroup_, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, timeout);
    if (bits & WIFI_CONNECTED_BIT) return ESP_OK;

    if (!persistent)
    {
        // if test failed we gotta stop retrying, or the STA keeps scanning and cooks the AP
        retryCount_ = MAX_RETRY;
        esp_wifi_disconnect();
    }
    return (bits & WIFI_FAIL_BIT) ? ESP_FAIL : ESP_ERR_TIMEOUT;
}

esp_err_t WifiHandler::startSoftAP(const char* ssid, const char* password)
{
    ESP_RETURN_ON_ERROR(init(), TAG, "init");

    const size_t ssidLen = ssid ? std::strlen(ssid) : 0;
    const size_t passLen = password ? std::strlen(password) : 0;
    ESP_RETURN_ON_FALSE(ssidLen > 0 && ssidLen <= MAX_SSID_LEN, ESP_ERR_INVALID_ARG, TAG, "AP ssid");
    ESP_RETURN_ON_FALSE(passLen <= MAX_PASS_LEN, ESP_ERR_INVALID_ARG, TAG, "AP password");

    wifi_config_t wifiConfig = {};
    std::memcpy(wifiConfig.ap.ssid, ssid, ssidLen);
    wifiConfig.ap.ssid_len = static_cast<uint8_t>(ssidLen);
    wifiConfig.ap.channel = 1;
    wifiConfig.ap.max_connection = 4;
    std::memcpy(wifiConfig.ap.password, password, passLen);
    wifiConfig.ap.authmode = passLen ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set_mode(AP)");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &wifiConfig), TAG, "set_config(AP)");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi_start(AP)");

    ESP_LOGI(TAG, "SoftAP started, ssid=%s", ssid);
    return ESP_OK;
}

void WifiHandler::eventHandler(void* arg, esp_event_base_t base, int32_t id, void* data)
{
    auto* self = static_cast<WifiHandler*>(arg);

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
    {
        // prov test prior to first success, 5 retries then fail
        if (!self->persistent_ && !self->everConnected_)
        {
            if (self->retryCount_ < MAX_RETRY)
            {
                esp_wifi_connect();
                self->retryCount_++;
                ESP_LOGI(TAG, "Retrying connection (%d/%d)", self->retryCount_.load(), MAX_RETRY);
            }
            else
            {
                xEventGroupSetBits(self->eventGroup_, WIFI_FAIL_BIT);
            }
            return;
        }

        // ptherwise never give up, back off timers are 1 2 4 8 16 30s
        const uint32_t delayMs = std::min<uint32_t>(1000u << std::min(self->retryCount_.load(), 5), 30000u);
        self->retryCount_++;
        const auto* info = static_cast<const wifi_event_sta_disconnected_t*>(data);
        ESP_LOGW(TAG, "Disconnected (reason %d), reconnecting in %u ms (attempt %d)",
                 info->reason, static_cast<unsigned>(delayMs), self->retryCount_.load());
        esp_timer_stop(self->reconnectTimer_);
        esp_timer_start_once(self->reconnectTimer_, static_cast<uint64_t>(delayMs) * 1000ULL);
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        self->retryCount_ = 0;
        self->everConnected_ = true;
        xEventGroupSetBits(self->eventGroup_, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Got IP.");
    }
}