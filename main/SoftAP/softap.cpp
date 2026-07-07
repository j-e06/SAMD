/*  WiFi softAP Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "config.cpp"


#define EXAMPLE_GTK_REKEY_INTERVAL CONFIG_ESP_GTK_REKEY_INTERVAL


static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        auto* event = static_cast<wifi_event_ap_staconnected_t *>(event_data);
        ESP_LOGI(WIFI_TAG, "station 'MACSTR' join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        auto* event = static_cast<wifi_event_ap_stadisconnected_t *>(event_data);
        ESP_LOGI(WIFI_TAG, "station 'MACSTR' leave, AID=%d, reason=%d",
                 MAC2STR(event->mac), event->aid, event->reason);
    }
}

void wifi_init_softap()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        nullptr,
                                                        nullptr));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = CONFIG_SOFTAP_DEFAULT_SSID,
            .password = CONFIG_SOFTAP_DEFAULT_PASSWORD,
            .ssid_len = static_cast<uint8_t>(strlen(CONFIG_SOFTAP_DEFAULT_SSID)),
            .channel = CONFIG_SOFTAP_ESP_WIFI_CHANNEL,
            .authmode = WIFI_AUTH_WPA3_PSK,
            .max_connection = CONFIG_SOFTAP_MAX_STA_CONN,
            .pmf_cfg = {
                .required = true,
            },
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
            .bss_max_idle_cfg = {
                .period = WIFI_AP_DEFAULT_MAX_IDLE_PERIOD,
                .protected_keep_alive = 1,
            },
        },
    };
    if (strlen(CONFIG_SOFTAP_DEFAULT_PASSWORD) == 0 && wifi_config.ap.authmode != WIFI_AUTH_OWE) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(WIFI_TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             CONFIG_SOFTAP_DEFAULT_SSID, CONFIG_SOFTAP_DEFAULT_PASSWORD, CONFIG_SOFTAP_ESP_WIFI_CHANNEL);
}

void app_main()
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(WIFI_TAG, "ESP_WIFI_MODE_AP");
    wifi_init_softap();
}