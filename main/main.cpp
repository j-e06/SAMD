#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "nvs.h"
#include <string>

#include "MAX30102/MAX30102.h"
#include "NTC/NTC.h"
#include "Provisioning/Provisioningserver.h"
#include "buttons/Buttons.h"
#include "mqtt/MQTTHandler.h"
#include "nvs/NvsHandler.h"
#include "processing/Processing.h"
#include "pump/pump.h"
#include "utilities.h"
#include "wifi/WifiHandler.h"

static const char *TAG = "MAIN";

static constexpr const char *SETUP_AP_SSID = CONFIG_SAMD_SETUP_AP_SSID;
static constexpr const char *SETUP_AP_PASS = CONFIG_SAMD_SETUP_AP_PASSWORD;
static_assert(sizeof(CONFIG_SAMD_SETUP_AP_PASSWORD) > 8,
              "WPA2 needs an AP password of at least 8 characters");

static constexpr const char *SYS_NAMESPACE = "sys";
static constexpr const char *FORCE_PROV_KEY = "force_prov";

static constexpr const char *SESSION_NVS_NS = "session";
static constexpr const char *SESSION_NVS_KEY = "active";

enum class NetMode { Provisioning, Station };


static NetMode start_provisioning(NvsHandler &nvs, WifiHandler &wifi) {
  ESP_ERROR_CHECK(wifi.startSoftAP(SETUP_AP_SSID, SETUP_AP_PASS));

  static ProvisioningServer provServer(nvs, wifi);

  provServer.onCredsSaved([](bool connected) {
    if (!connected)
      return;

    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
  });

  ESP_ERROR_CHECK(provServer.start());

  return NetMode::Provisioning;
}

static NetMode start_network(NvsHandler &nvs, WifiHandler &wifi) {
  ESP_ERROR_CHECK(wifi.init());
  // request while in error mode, keep old creds in nvs until new ones are 100% to work.
  // power cycle goes to old network.
  // note, we only provision creds once, not repeatedly
  std::string flag;
  const bool forced =
      nvs.getString(SYS_NAMESPACE, FORCE_PROV_KEY, flag) == ESP_OK &&
      flag == "1";
  if (forced) {
    nvs.eraseNamespace(SYS_NAMESPACE);
    ESP_LOGW(TAG, "Wifi configuration requested.");
  }
  if (forced || !wifi.hasStoredCreds())
    return start_provisioning(nvs, wifi);

  // waits up to 15 s, then keeps retrying in the background forever
  // not sure if best way, but it's done.
  const esp_err_t err = wifi.startStation(pdMS_TO_TICKS(15000));

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Connected to wifi.");
  } else if (err == ESP_ERR_TIMEOUT) {
    ESP_LOGW(TAG, "Not connected yet, retrying in background.");
  } else {
    // if we got here, stored creds are bad or driver isn't working with them

    ESP_LOGE(TAG, "Station start failed: %s", esp_err_to_name(err));
    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_INVALID_SIZE)
      wifi.clearCreds();

    return start_provisioning(nvs, wifi);
  }
  return NetMode::Station;
}

// broker chosen during provisioning, else the build-time default
static std::string broker_uri(NvsHandler &nvs) {
  std::string uri;
  if (nvs.getString(BROKER_NVS_NS, BROKER_NVS_KEY, uri) != ESP_OK ||
      uri.empty())
    uri = DEFAULT_BROKER_URI;
  return uri;
}
extern "C" void app_main(void) {
  ESP_LOGI(TAG, "Starting main.");

  // pump init first so we know it's off before anything gets the chance to block it
  static Pump pump;

  static NvsHandler nvs;
  ESP_ERROR_CHECK(nvs.init());
  static WifiHandler wifi(nvs);

  // THE queue
  QueueHandle_t queue = xQueueCreate(16, sizeof(queue_entry));
  ESP_ERROR_CHECK(queue ? ESP_OK : ESP_ERR_NO_MEM);

  // start wifi network
  const bool station = (start_network(nvs, wifi) == NetMode::Station);

  // mqtt only runs while we're in station mode, it self attempts connections to wifi once wifi has an ip

  static MQTTHandler mqtt(queue);
  static const std::string brokerUri = broker_uri(nvs);
  if (station)
    ESP_ERROR_CHECK(mqtt.begin(brokerUri.c_str()));

  // everything below is satic, main returns but tasks keep rönning
  static Processing processing(queue, station ? &mqtt : nullptr, &pump);
  processing.onWifiConfigRequest([] {
    nvs.setString(SYS_NAMESPACE, FORCE_PROV_KEY, "1");
    esp_restart();
  });

  // if power is cut during a session, we go straight back into the session once sensors are up
  // if we're NOT in "session"/ station mode, we clear the flag
  std::string sessionFlag;
  const bool interrupted =
      nvs.getString(SESSION_NVS_NS, SESSION_NVS_KEY, sessionFlag) == ESP_OK &&
      sessionFlag == "1";

  if (interrupted && station) {
    ESP_LOGW(TAG, "Previous session was interrupted, recovering paused.");
    processing.resumeSessionAfterInit();
  } else if (interrupted) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        nvs.setString(SESSION_NVS_NS, SESSION_NVS_KEY, "0"));
  }
  processing.onSessionActive([](bool active) {
    nvs.setString(SESSION_NVS_NS, SESSION_NVS_KEY, active ? "1" : "0");
  });

  ESP_ERROR_CHECK(processing.begin(station ? Processing::State::Init
                                           : Processing::State::Provisioning));
  static Buttons buttons(queue);
  ESP_ERROR_CHECK(buttons.begin());

  static Thermistor ntc(queue);
  ESP_ERROR_CHECK_WITHOUT_ABORT(ntc.begin());

  static MAX30102 pulse(queue);
  ESP_ERROR_CHECK_WITHOUT_ABORT(pulse.begin());

}
