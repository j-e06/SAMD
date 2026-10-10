#include "NvsHandler.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs_handle.hpp"

static const char* TAG = "NvsHandler";

esp_err_t NvsHandler::init()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase nvs due to no space");
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t NvsHandler::setString(const char* ns, const char* key, const std::string& value)
{
    esp_err_t err;
    auto handle = nvs::open_nvs_handle(ns, NVS_READWRITE, &err);
    ESP_RETURN_ON_ERROR(err, TAG, "open %s", ns);
    ESP_RETURN_ON_ERROR(handle->set_string(key, value.c_str()), TAG, "set %s", key);
    return handle->commit();
}

esp_err_t NvsHandler::getString(const char* ns, const char* key, std::string& value)
{
    esp_err_t err;
    auto handle = nvs::open_nvs_handle(ns, NVS_READONLY, &err);
    if (err != ESP_OK) return err;   // give ESP_ERR_NVS_NOT_FOUND if no namespace exists

    char buf[128];                   // ssid 32 + password 64 + the sys flag all fit, longer gets errored out.
    err = handle->get_string(key, buf, sizeof buf);
    if (err == ESP_OK) value = buf;
    return err;
}

esp_err_t NvsHandler::eraseNamespace(const char* ns)
{
    esp_err_t err;
    auto handle = nvs::open_nvs_handle(ns, NVS_READWRITE, &err);
    if (err != ESP_OK) return err;
    ESP_RETURN_ON_ERROR(handle->erase_all(), TAG, "erase %s", ns);
    return handle->commit();
}