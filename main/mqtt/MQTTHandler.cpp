#include "MQTTHandler.h"
#include "esp_check.h"
#include "esp_log.h"
#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <optional>
#include <string_view>

static const char *TAG = "MQTT Handler";
#ifdef SAMD_HAVE_BROKER_CA
// main/certs/broker_ca.pem, embedded by main/CMakeLists.txt
extern const char brokerCa[] asm("_binary_broker_ca_pem_start");
#else
#include "esp_crt_bundle.h"
#endif
// exactly one of the command words (trailing whitespace tolerated) or just nada
static std::optional<Command> parseCommand(const char *data, int len) {
  std::string_view word(data, len);
  while (!word.empty() && std::isspace(static_cast<unsigned char>(word.back())))
    word.remove_suffix(1);

  if (word == "start")
    return Command::Start;
  if (word == "pause")
    return Command::Pause;
  if (word == "continue")
    return Command::Continue;
  if (word == "stop")
    return Command::Stop;
  return std::nullopt;
}

// ms since boot: the clock of every timestamp we send
static int64_t nowMs() { return esp_timer_get_time() / 1000; }
esp_err_t MQTTHandler::begin(const char *brokerUri) {
  ESP_LOGI(TAG, "Broker %s", brokerUri);
  esp_mqtt_client_config_t config = {};
  config.broker.address.uri = brokerUri;
  config.session.keepalive =
      30; // keep alive thing, sends msg to server in approx 30s * 1.5 if no response from device
  config.network.reconnect_timeout_ms = 3000;
  config.network.timeout_ms = 10000;

  if (CONFIG_SAMD_MQTT_USERNAME[0] != '\0') {
    config.credentials.username = CONFIG_SAMD_MQTT_USERNAME;
    config.credentials.authentication.password = CONFIG_SAMD_MQTT_PASSWORD;
  }

  if (std::string_view(brokerUri).rfind("mqtts://", 0) == 0) {
    if (CONFIG_SAMD_MQTT_TLS_NAME[0] != '\0') {
      config.broker.verification.common_name = CONFIG_SAMD_MQTT_TLS_NAME;
    }
#ifdef SAMD_HAVE_BROKER_CA
    config.broker.verification.certificate = brokerCa;
#else
    config.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
#endif
  } else {
    ESP_LOGW(TAG,
             "Plain mqtt:// broker, traffic not encrypted. please use mqtts :)");
  }

  // actual last will thing, sent to server if device dies / loses connection for w/e reason.
  config.session.last_will.topic = STATUS_TOPIC;
  config.session.last_will.msg =
      "{\"device_id\":\"" DEVICE_ID "\",\"state\":\"offline\"}";
  config.session.last_will.msg_len = 0; // 0 = strlen
  config.session.last_will.qos = 1;
  config.session.last_will.retain = 1;

  client_ = esp_mqtt_client_init(&config);
  ESP_RETURN_ON_FALSE(client_, ESP_ERR_NO_MEM, TAG, "client init");

  ESP_RETURN_ON_ERROR(
      esp_mqtt_client_register_event(
          client_,
          MQTT_EVENT_ANY,
          &MQTTHandler::mqttEventHandler,
          this),
      TAG,
      "register mqtt events");

  ESP_RETURN_ON_ERROR(
      esp_mqtt_client_start(client_),
      TAG,
      "start mqtt");

  return ESP_OK;
}

void MQTTHandler::mqttEventHandler(void *arg, esp_event_base_t base, int32_t id,
                                   void *data) {
  static_cast<MQTTHandler *>(arg)->handleMqttEvent(
      static_cast<esp_mqtt_event_handle_t>(data));
}
void MQTTHandler::handleMqttEvent(esp_mqtt_event_handle_t event) {
  switch (event->event_id) {

  case MQTT_EVENT_CONNECTED: {
    connected_ = true;
    ESP_LOGI(TAG, "MQTT Connected");

    if (esp_mqtt_client_subscribe_single(client_, CMD_TOPIC, 1) < 0) {
      ESP_LOGE(TAG, "Failed to subscribe to '%s'", CMD_TOPIC);
    }

      // the broke might retain last will as status, check dat
    if (const char *state = lastState_)
      publishStatus(state);
    break;
  }

  case MQTT_EVENT_DISCONNECTED:
    connected_ = false;
    ESP_LOGI(TAG, "MQTT Disconnected");
    break;

  case MQTT_EVENT_DATA: {
    ESP_LOGI(TAG, "MQTT Data: topic='%.*s', data='%.*s'", event->topic_len,
             event->topic, event->data_len, event->data);

    const bool isCmd =
        std::string_view(event->topic, event->topic_len) == CMD_TOPIC;
    if (!isCmd || event->data_len <= 0 || event->data_len > 32 ||
        event->current_data_offset != 0 ||
        event->data_len != event->total_data_len) {
      break;
    }

      // gotta ignore retained command due to them being replayed every reconnect, would mess with session handling logic
    if (event->retain) {
      ESP_LOGW(TAG, "Ignoring retained command");
      break;
    }

    const auto cmd = parseCommand(event->data, event->data_len);
    if (!cmd) {
      ESP_LOGW(TAG, "Ignoring unknown command '%.*s'", event->data_len,
               event->data);
      break;
    }

    auto e = make_entry(EventType::Command);
    e.command.command = *cmd;
    if (!post_event(cmdQueue_, e))
      ESP_LOGW(TAG, "Command dropped, queue full");
    break;
  }

  case MQTT_EVENT_ERROR:
    if (event->error_handle) {
      ESP_LOGE(TAG, "error_type=%d tls_err=0x%x sock_errno=%d",
               event->error_handle->error_type,
               event->error_handle->esp_tls_last_esp_err,
               event->error_handle->esp_transport_sock_errno);
    }
    break;

  default:
    break;
  }
}

bool MQTTHandler::publish(const combined_data &d) {
  if (!connected_)
    return false;

  char hr[12] = "null", spo2[12] = "null", temp[16] = "null";
  if (d.hr)
    snprintf(hr, sizeof hr, "%" PRId32, *d.hr);
  if (d.spo2)
    snprintf(spo2, sizeof spo2, "%" PRId32, *d.spo2);
  if (d.temp)
    snprintf(temp, sizeof temp, "%.2f", *d.temp);

  char payload[160];
  const int n =
      snprintf(payload, sizeof payload,
               "{\"timestamp\":%" PRId64
               ",\"hr\":%s,\"spo2\":%s,\"temp\":%s,\"device_id\":\"%s\"}",
               nowMs(), hr, spo2, temp, DEVICE_ID);
  if (n < 0 || n >= static_cast<int>(sizeof payload)) {
    ESP_LOGE(TAG, "payload truncated, not publishing");
    return false;
  }

  const int msg_id =
      esp_mqtt_client_enqueue(client_, DATA_TOPIC, payload, 0, 1, 0, true);
  if (msg_id < 0) {
    ESP_LOGE(TAG, "Failed to publish message, msg id: %d", msg_id);
    return false;
  }
  return true;
}

bool MQTTHandler::publishStatus(const char *state, const char *detail) {
  lastState_ = state;
  if (!connected_)
    return false;

  char detailField[48] = "";
  if (detail)
    snprintf(detailField, sizeof detailField, ",\"detail\":\"%s\"", detail);

  char payload[128];
  const int n = snprintf(
      payload, sizeof payload,
      "{\"device_id\":\"%s\",\"state\":\"%s\"%s,\"timestamp\":%" PRId64 "}",
      DEVICE_ID, state, detailField, nowMs());
  if (n < 0 || n >= static_cast<int>(sizeof payload)) {
    ESP_LOGE(TAG, "status payload truncated");
    return false;
  }
  return esp_mqtt_client_enqueue(client_, STATUS_TOPIC, payload, 0, 1, 1,
                                 true) >= 0;
}
