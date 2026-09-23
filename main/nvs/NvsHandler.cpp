#include "NvsHandler.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char* TAG = "NvsHandler";

esp_err_t NvsHandler::init()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        err = nvs_flash_erase();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "erase failed: %s", esp_err_to_name(err));
            return err;
        }
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t NvsHandler::setString(const char* ns, const char* key, const std::string& value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "open failed for ns=%s: %s", ns, esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(handle, key, value.c_str());
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

esp_err_t NvsHandler::getString(const char* ns, const char* key, std::string& outValue)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        return err;  // ESP_ERR_NVS_NOT_FOUND if namespace doesn't exist yet
    }

    size_t requiredSize = 0;
    err = nvs_get_str(handle, key, nullptr, &requiredSize);
    if (err != ESP_OK)
    {
        nvs_close(handle);
        return err;
    }

    outValue.resize(requiredSize);
    err = nvs_get_str(handle, key, &outValue[0], &requiredSize);
    if (err == ESP_OK && !outValue.empty() && outValue.back() == '\0')
    {
        outValue.resize(requiredSize - 1);  // drop the null terminator nvs includes in size
    }

    nvs_close(handle);
    return err;
}

esp_err_t NvsHandler::eraseNamespace(const char* ns)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_erase_all(handle);
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}