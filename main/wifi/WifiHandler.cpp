#include "WifiHandler.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include <cstring>

static const char* TAG = "WifiHandler";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

WifiHandler::WifiHandler(NvsHandler& nvs) : nvs_(nvs){}

void WifiHandler::init()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    eventGroup_ = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT,ESP_EVENT_ANY_ID, &WifiHandler::eventHandler, this, nullptr)
        );
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiHandler::eventHandler, this, nullptr)
        );
    ESP_LOGI(TAG, "Init complete.");
}

void startSoftAP(const char* ssid, const char* password)
{

}

bool WifiHandler::hasStoredCreds()
{
    std::string ssid;
    return nvs_.getString(NVS_NAMESPACE, "ssid", ssid) == ESP_OK && !ssid.empty();
}

esp_err_t WifiHandler::saveCreds(const std::string& ssid, const std::string& password)
{
    esp_err_t err = nvs_.setString(NVS_NAMESPACE, "ssid", ssid);
    if (err != ESP_OK) return err;
    return nvs_.setString(NVS_NAMESPACE, "password", password);
}

bool WifiHandler::startStation()
{
    std::string ssid, password;
    if (nvs_.getString(NVS_NAMESPACE, "ssid", ssid) != ESP_OK ||
        nvs_.getString(NVS_NAMESPACE, "password", password) != ESP_OK )
        {
        ESP_LOGE(TAG, "No stored creds.");
        return false;
        }
    esp_netif_create_default_wifi_sta();
    wifi_config_t wifiConfig = {};
    strncpy(reinterpret_cast<char*>(wifiConfig.sta.ssid), ssid.c_str(), sizeof(wifiConfig.sta.ssid));
    strncpy(reinterpret_cast<char*>(wifiConfig.sta.password), password.c_str(), sizeof(wifiConfig.sta.password));
    wifiConfig.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifiConfig));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(eventGroup_,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE,pdFALSE,portMAX_DELAY);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

void WifiHandler::eventHandler(void* arg, esp_event_base_t base, int32_t id, void *data)
{
    auto *self = static_cast<WifiHandler*>(arg);

    if (base ==WIFI_EVENT && id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (self->retryCount_ < MAX_RETRY)
        {
            esp_wifi_connect();
            self->retryCount_++;
            ESP_LOGI(TAG, "Retrying connection (%d/%d)", self->retryCount_, MAX_RETRY);
        } else
        {
            xEventGroupSetBits(self->eventGroup_, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        self->retryCount_ = 0;
        xEventGroupSetBits(self->eventGroup_, WIFI_CONNECTED_BIT);
    }
}