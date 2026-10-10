#include "Provisioningserver.h"
#include <cctype>
#include <cstdlib>
#include "esp_check.h"
#include "esp_log.h"
#include "utilities.h"

static const char* TAG = "ProvisioningServer";

namespace {

constexpr size_t FIELD_BUF_LEN = 256;   // raw still url encoded value of one form field
constexpr size_t MAX_BODY_LEN = 512;
// "%41" -> 'A', "+" -> ' '. esp_http_server splits the form but doesnt decode it.
std::string urlDecode(const char* s)
{
    std::string out;
    for (; *s; ++s)
    {
        char c = *s;
        if (c == '+')
        {
            c = ' ';
        }
        else if (c == '%' && std::isxdigit((unsigned char)s[1]) && std::isxdigit((unsigned char)s[2]))
        {
            const char hex[3] = { s[1], s[2], '\0' };
            c = static_cast<char>(std::strtol(hex, nullptr, 16));
            s += 2;
        }
        if (c) out += c;
    }
    return out;
}

esp_err_t readRequestBody(httpd_req_t* req, std::string& body)
{
    if (req->content_len == 0 || req->content_len > MAX_BODY_LEN)
    {
        ESP_LOGW(TAG, "bad content_len: %d", static_cast<int>(req->content_len));
        return ESP_ERR_INVALID_SIZE;
    }

    body.resize(req->content_len);
    size_t received = 0;
    int timeouts = 0;
    while (received < req->content_len)
    {
        int ret = httpd_req_recv(req, &body[received], req->content_len - received);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 3) continue;
        if (ret <= 0)
        {
            ESP_LOGW(TAG, "httpd_req_recv failed: %d", ret);
            return ESP_FAIL;
        }
        received += static_cast<size_t>(ret);
    }
    return ESP_OK;
}

    // check key in urlencoded form body and decode. false if missing or overtly long
bool formField(const std::string& body, const char* key, std::string& out)
{
    char raw[FIELD_BUF_LEN];
    if (httpd_query_key_value(body.c_str(), key, raw, sizeof raw) != ESP_OK) return false;
    out = urlDecode(raw);
    return true;
}
    // mqtt:// or mqtts:// and short enough
    bool validBrokerUri(const std::string& uri)
{
    const bool scheme = uri.rfind("mqtt://", 0) == 0 || uri.rfind("mqtts://", 0) == 0;
    return scheme && uri.size() <= 127;
}

esp_err_t sendPage(httpd_req_t* req, const char* html)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

    constexpr const char* kFormPage =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<title>Device setup</title></head><body>"
        "<h2>Device setup</h2>"
        "<form method=\"POST\" action=\"/save\">"
        "Wi-Fi name (SSID):<br><input name=\"ssid\" maxlength=\"32\"><br>"
        "Wi-Fi password:<br><input name=\"password\" type=\"password\" maxlength=\"64\"><br>"
        "Server address, e.g. mqtts://192.168.1.10:8883 (empty = keep current):<br>"
        "<input name=\"broker\" maxlength=\"127\"><br><br>"
        "<button type=\"submit\">Save</button>"
        "</form></body></html>";
// response to /save, refreshed to /status until attempt done,
constexpr const char* kConnectingPage =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta http-equiv=\"refresh\" content=\"2;url=/status\"></head>"
    "<body><h2>Connecting...</h2><p>Trying the network now, hang on.</p></body></html>";

constexpr const char* kSuccessPage =
    "<!DOCTYPE html><html><body><h2>Connected.</h2>"
    "<p>The device joined the network. It will restart shortly.</p></body></html>";

constexpr const char* kConnectFailedPage =
    "<!DOCTYPE html><html><body><h2>Couldn't connect.</h2>"
    "<p>Nothing was saved. Check the network name, password and signal, "
    "then <a href=\"/\">try again</a>.</p></body></html>";

}  // namespace

esp_err_t ProvisioningServer::start()
{
    if (server_) return ESP_OK;   // already running

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    ESP_RETURN_ON_ERROR(httpd_start(&server_, &config), TAG, "httpd_start");

    const httpd_uri_t routes[] = {
        { .uri = "/",       .method = HTTP_GET,  .handler = rootGetHandler,      .user_ctx = this },
        { .uri = "/save",   .method = HTTP_POST, .handler = saveCredsPostHandler, .user_ctx = this },
        { .uri = "/status", .method = HTTP_GET,  .handler = statusGetHandler,    .user_ctx = this },
    };
    for (const auto& route : routes)
    {
        esp_err_t err = httpd_register_uri_handler(server_, &route);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "uri registration failed: %s", esp_err_to_name(err));
            httpd_stop(server_);
            server_ = nullptr;
            return err;
        }
    }

    ESP_LOGI(TAG, "Provisioning server started.");
    return ESP_OK;
}

esp_err_t ProvisioningServer::rootGetHandler(httpd_req_t* req)
{
    return sendPage(req, kFormPage);
}

esp_err_t ProvisioningServer::statusGetHandler(httpd_req_t* req)
{
    auto* self = static_cast<ProvisioningServer*>(req->user_ctx);

    switch (self->status_.load())
    {
        case ConnectStatus::Connected: return sendPage(req, kSuccessPage);
        case ConnectStatus::Failed:    return sendPage(req, kConnectFailedPage);
        default:                       return sendPage(req, kConnectingPage);
    }
}
esp_err_t ProvisioningServer::saveCredsPostHandler(httpd_req_t* req)
{
    auto* self = static_cast<ProvisioningServer*>(req->user_ctx);

    // an attempt is already running just show its progress
    if (self->status_ == ConnectStatus::Connecting) return sendPage(req, kConnectingPage);

    std::string body;
    if (readRequestBody(req, body) != ESP_OK ||
        !formField(body, "ssid", self->pendingSsid_) || self->pendingSsid_.empty())
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid SSID");
    }
    self->pendingPass_.clear();
    formField(body, "password", self->pendingPass_);   // optional: open networks have none
    self->pendingBroker_.clear();
    formField(body, "broker", self->pendingBroker_);   // optional: empty keeps the current one
    if (!self->pendingBroker_.empty() && !validBrokerUri(self->pendingBroker_))
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Server address must look like mqtt://host:1883");
    }

    // try creds in cTE so we can give user the response page fast
    self->status_ = ConnectStatus::Connecting;
    if (xTaskCreate(&ProvisioningServer::connectTaskEntry, "wifi_connect",
                    4096, self, tskIDLE_PRIORITY + 1, nullptr) != pdPASS)
    {
        ESP_LOGE(TAG, "failed to start connect task");
        self->status_ = ConnectStatus::Failed;
        return sendPage(req, kConnectFailedPage);
    }
    return sendPage(req, kConnectingPage);
}
void ProvisioningServer::connectTaskEntry(void* arg)
{
    auto* self = static_cast<ProvisioningServer*>(arg);

    esp_err_t err = self->wifi_.tryStation(self->pendingSsid_, self->pendingPass_, CONNECT_TIMEOUT);
    if (err == ESP_OK && !self->pendingBroker_.empty())
    {
        err = self->nvs_.setString(BROKER_NVS_NS, BROKER_NVS_KEY, self->pendingBroker_);
    }
    if (err == ESP_OK)
    {
        err = self->wifi_.saveCreds(self->pendingSsid_, self->pendingPass_);   // last: it is what marks the device as configured
    }
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "provisioning failed: %s", esp_err_to_name(err));
    }
    self->pendingPass_.clear();

    const bool ok = (err == ESP_OK);
    self->status_ = ok ? ConnectStatus::Connected : ConnectStatus::Failed;
    if (self->credsSavedCb_) self->credsSavedCb_(ok);

    vTaskDelete(nullptr);
}