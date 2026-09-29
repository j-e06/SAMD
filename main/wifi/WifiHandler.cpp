#include "WifiHandler.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include <cstring>
#include <nvs.h>

static const char* TAG = "WifiHandler";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

// Most functions below are just "call the driver, and bail out with a log
// line if it complains." These two macros say that once instead of retyping
// it at every call site.

#define RETURN_ON_ERR(expr, msg)                                \
    do {                                                         \
        esp_err_t _err = (expr);                                  \
        if (_err != ESP_OK)                                       \
        {                                                          \
            ESP_LOGE(TAG, msg ": %s", esp_err_to_name(_err));       \
            return _err;                                           \
        }                                                           \
    } while (0)

#define RETURN_ON_ERR_UNLESS(expr, ok, msg)                     \
    do {                                                         \
        esp_err_t _err = (expr);                                  \
        if (_err != ESP_OK && _err != (ok))                       \
        {                                                          \
            ESP_LOGE(TAG, msg ": %s", esp_err_to_name(_err));       \
            return _err;                                           \
        }                                                           \
    } while (0)

WifiHandler::WifiHandler(NvsHandler& nvs) : nvs_(nvs)
{
}

WifiHandler::~WifiHandler()
{
    if (wifiEventInstance_)
    {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifiEventInstance_);
    }
    if (ipEventInstance_)
    {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ipEventInstance_);
    }
    if (eventGroup_)
    {
        vEventGroupDelete(eventGroup_);
    }
}

esp_err_t WifiHandler::init()
{
    if (initialized_) return ESP_OK;

    RETURN_ON_ERR_UNLESS(esp_netif_init(), ESP_ERR_INVALID_STATE, "netif init failed");
    RETURN_ON_ERR_UNLESS(esp_event_loop_create_default(), ESP_ERR_INVALID_STATE, "event loop create failed");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    RETURN_ON_ERR(esp_wifi_init(&cfg), "wifi init failed");

    eventGroup_ = xEventGroupCreate();
    if (!eventGroup_)
    {
        ESP_LOGE(TAG, "failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    RETURN_ON_ERR(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiHandler::eventHandler, this, &wifiEventInstance_),
        "wifi event register failed");
    RETURN_ON_ERR(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiHandler::eventHandler, this, &ipEventInstance_),
        "ip event register failed");

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
    if (ssid.empty() || ssid.size() > MAX_SSID_LEN)
    {
        ESP_LOGE(TAG, "ssid length invalid (%d)", static_cast<int>(ssid.size()));
        return ESP_ERR_INVALID_ARG;
    }
    if (password.size() > MAX_PASS_LEN)
    {
        ESP_LOGE(TAG, "password too long (%d)", static_cast<int>(password.size()));
        return ESP_ERR_INVALID_ARG;
    }

    RETURN_ON_ERR(nvs_.setString(NVS_NAMESPACE, "ssid", ssid), "saving ssid failed");
    return nvs_.setString(NVS_NAMESPACE, "password", password);
}

esp_err_t WifiHandler::startStation(TickType_t timeout)
{
    if (!initialized_) RETURN_ON_ERR(init(), "init failed");

    std::string ssid, password;
    if (nvs_.getString(NVS_NAMESPACE, "ssid", ssid) != ESP_OK ||
        nvs_.getString(NVS_NAMESPACE, "password", password) != ESP_OK)
    {
        ESP_LOGE(TAG, "No stored creds.");
        return ESP_ERR_NVS_NOT_FOUND;
    }

    if (ssid.size() > MAX_SSID_LEN || password.size() > MAX_PASS_LEN)
    {
        ESP_LOGE(TAG, "Stored creds too long for wifi_config_t.");
        return ESP_ERR_INVALID_SIZE;
    }

    wifi_mode_t currentMode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&currentMode);
    bool apActive = (currentMode == WIFI_MODE_AP || currentMode == WIFI_MODE_APSTA);

    esp_netif_create_default_wifi_sta();

    wifi_config_t wifiConfig = {};
    std::memcpy(wifiConfig.sta.ssid, ssid.c_str(), ssid.size());
    std::memcpy(wifiConfig.sta.password, password.c_str(), password.size());
    wifiConfig.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    RETURN_ON_ERR(esp_wifi_set_mode(apActive ? WIFI_MODE_APSTA : WIFI_MODE_STA), "set_mode failed");
    RETURN_ON_ERR(esp_wifi_set_config(WIFI_IF_STA, &wifiConfig), "set_config failed");

    // Coming from AP mode, the driver's already running -- esp_wifi_start()
    // reports that as ESP_ERR_WIFI_CONN, which isn't a real failure here.
    RETURN_ON_ERR_UNLESS(esp_wifi_start(), ESP_ERR_WIFI_CONN, "wifi_start failed");

    // Clear any bits left over from a previous attempt so we don't
    // immediately "succeed" or "fail" on a stale result.
    retryCount_ = 0;
    xEventGroupClearBits(eventGroup_, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    EventBits_t bits = xEventGroupWaitBits(
        eventGroup_, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, timeout);

    if (bits & WIFI_CONNECTED_BIT) return ESP_OK;
    if (bits & WIFI_FAIL_BIT) return ESP_FAIL;
    return ESP_ERR_TIMEOUT;  // neither bit set: wait timed out
}

esp_err_t WifiHandler::startSoftAP(const char* ssid, const char* password)
{
    if (!initialized_) RETURN_ON_ERR(init(), "init failed");

    if (!ssid || std::strlen(ssid) == 0 || std::strlen(ssid) > MAX_SSID_LEN)
    {
        ESP_LOGE(TAG, "invalid AP ssid");
        return ESP_ERR_INVALID_ARG;
    }
    size_t passLen = password ? std::strlen(password) : 0;
    if (passLen > MAX_PASS_LEN)
    {
        ESP_LOGE(TAG, "AP password too long");
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_create_default_wifi_ap();

    wifi_config_t wifiConfig = {};
    std::memcpy(wifiConfig.ap.ssid, ssid, std::strlen(ssid));
    wifiConfig.ap.ssid_len = static_cast<uint8_t>(std::strlen(ssid));
    wifiConfig.ap.channel = 1;
    wifiConfig.ap.max_connection = 4;

    if (passLen > 0)
    {
        std::memcpy(wifiConfig.ap.password, password, passLen);
        wifiConfig.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }
    else
    {
        wifiConfig.ap.authmode = WIFI_AUTH_OPEN;
    }

    RETURN_ON_ERR(esp_wifi_set_mode(WIFI_MODE_AP), "set_mode(AP) failed");
    RETURN_ON_ERR(esp_wifi_set_config(WIFI_IF_AP, &wifiConfig), "set_config(AP) failed");
    RETURN_ON_ERR(esp_wifi_start(), "wifi_start(AP) failed");

    ESP_LOGI(TAG, "SoftAP started, ssid=%s", ssid);
    return ESP_OK;
}

void WifiHandler::eventHandler(void* arg, esp_event_base_t base, int32_t id, void* data)
{
    auto* self = static_cast<WifiHandler*>(arg);

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
    {
        self->connected_ = false;
        if (self->retryCount_ < MAX_RETRY)
        {
            esp_wifi_connect();
            self->retryCount_++;
            ESP_LOGI(TAG, "Retrying connection (%d/%d)", self->retryCount_, MAX_RETRY);
        }
        else
        {
            xEventGroupSetBits(self->eventGroup_, WIFI_FAIL_BIT);
        }
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        self->retryCount_ = 0;
        self->connected_ = true;
        xEventGroupSetBits(self->eventGroup_, WIFI_CONNECTED_BIT);
    }
}