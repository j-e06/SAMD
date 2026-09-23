#include "WifiHandler.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include <cstring>
#include <nvs.h>

static const char* TAG = "WifiHandler";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

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

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "netif init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "event loop create failed: %s", esp_err_to_name(err));
        return err;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "wifi init failed: %s", esp_err_to_name(err));
        return err;
    }

    eventGroup_ = xEventGroupCreate();
    if (!eventGroup_)
    {
        ESP_LOGE(TAG, "failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    err = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiHandler::eventHandler, this, &wifiEventInstance_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "wifi event register failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiHandler::eventHandler, this, &ipEventInstance_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "ip event register failed: %s", esp_err_to_name(err));
        return err;
    }

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

    esp_err_t err = nvs_.setString(NVS_NAMESPACE, "ssid", ssid);
    if (err != ESP_OK) return err;
    return nvs_.setString(NVS_NAMESPACE, "password", password);
}

esp_err_t WifiHandler::startStation(TickType_t timeout)
{
    if (!initialized_)
    {
        esp_err_t err = init();
        if (err != ESP_OK) return err;
    }

    std::string ssid, password;
    if (nvs_.getString(NVS_NAMESPACE, "ssid", ssid) != ESP_OK ||
        nvs_.getString(NVS_NAMESPACE, "password", password) != ESP_OK)
    {
        ESP_LOGE(TAG, "No stored creds.");
        return ESP_ERR_NVS_NOT_FOUND;
    }

    // Guard against stored values that no longer fit the driver's fixed buffers
    // (strncpy would otherwise truncate without a null terminator).
    if (ssid.size() > MAX_SSID_LEN || password.size() > MAX_PASS_LEN)
    {
        ESP_LOGE(TAG, "Stored creds too long for wifi_config_t.");
        return ESP_ERR_INVALID_SIZE;
    }

    esp_netif_create_default_wifi_sta();

    wifi_config_t wifiConfig = {};
    std::memcpy(wifiConfig.sta.ssid, ssid.c_str(), ssid.size());
    std::memcpy(wifiConfig.sta.password, password.c_str(), password.size());
    wifiConfig.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "set_mode failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &wifiConfig);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "set_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "wifi_start failed: %s", esp_err_to_name(err));
        return err;
    }

    // Reset state before waiting so a second call doesn't immediately
    // return on bits left over from a previous connection attempt.
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
    if (!initialized_)
    {
        esp_err_t err = init();
        if (err != ESP_OK) return err;
    }

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

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "set_mode(AP) failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &wifiConfig);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "set_config(AP) failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "wifi_start(AP) failed: %s", esp_err_to_name(err));
        return err;
    }

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