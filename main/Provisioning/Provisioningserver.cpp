#include "Provisioningserver.h"
#include "esp_log.h"
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

static const char* TAG = "ProvisioningServer";

namespace {

constexpr size_t kSsidFieldBufLen = 128;
constexpr size_t kPasswordFieldBufLen = 256;

std::string urlDecode(const char* src, size_t len)
{
    std::string out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i)
    {
        char c = src[i];
        if (c == '+')
        {
            out += ' ';
        }
        else if (c == '%' && i + 2 < len)
        {
            char hex[3] = { src[i + 1], src[i + 2], '\0' };
            char* end = nullptr;
            long value = strtol(hex, &end, 16);
            if (end == hex + 2)
            {
                out += static_cast<char>(value);
                i += 2;
            }
            else
            {
                out += c;  // malformed escape, keep literal
            }
        }
        else
        {
            out += c;
        }
    }
    return out;
}

esp_err_t readRequestBody(httpd_req_t* req, std::string& outBody, size_t maxLen)
{
    if (req->content_len == 0 || req->content_len > maxLen)
    {
        ESP_LOGW(TAG, "bad content_len: %d", static_cast<int>(req->content_len));
        return ESP_ERR_INVALID_SIZE;
    }

    outBody.resize(req->content_len);
    size_t received = 0;
    while (received < req->content_len)
    {
        int ret = httpd_req_recv(req, &outBody[received], req->content_len - received);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (ret <= 0)
        {
            ESP_LOGW(TAG, "httpd_req_recv failed: %d", ret);
            return ESP_FAIL;
        }
        received += static_cast<size_t>(ret);
    }
    return ESP_OK;
}

// Looks up `key` in a urlencoded form body and decodes it into `outValue`.
// Returns false if the field is missing or too long for `rawBufSize`.
bool extractFormField(const std::string& body, const char* key, size_t rawBufSize, std::string& outValue)
{
    std::vector<char> raw(rawBufSize, '\0');
    if (httpd_query_key_value(body.c_str(), key, raw.data(), raw.size()) != ESP_OK)
    {
        return false;
    }
    outValue = urlDecode(raw.data(), strlen(raw.data()));
    return true;
}

esp_err_t sendPage(httpd_req_t* req, const char* html, const char* httpStatus = nullptr)
{
    if (httpStatus) httpd_resp_set_status(req, httpStatus);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

const char* kFormPage =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<title>Wi-Fi setup</title></head><body>"
    "<h2>Wi-Fi setup</h2>"
    "<form method=\"POST\" action=\"/save\">"
    "SSID:<br><input name=\"ssid\" maxlength=\"32\"><br>"
    "Password:<br><input name=\"password\" type=\"password\" maxlength=\"64\"><br><br>"
    "<button type=\"submit\">Save</button>"
    "</form></body></html>";

const char* kRedirectToStatusPage =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta http-equiv=\"refresh\" content=\"1;url=/status\"></head>"
    "<body><p>Saving...</p></body></html>";

const char* kConnectingPage =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta http-equiv=\"refresh\" content=\"2;url=/status\"></head>"
    "<body><h2>Connecting...</h2><p>Trying the network now, hang on.</p></body></html>";

const char* kSuccessPage =
    "<!DOCTYPE html><html><body><h2>Connected.</h2>"
    "<p>The device joined the network. It will restart shortly.</p></body></html>";

const char* kConnectFailedPage =
    "<!DOCTYPE html><html><body><h2>Couldn't connect.</h2>"
    "<p>Saved, but the device couldn't join that network -- double check the "
    "password and signal, then <a href=\"/\">try again</a>.</p></body></html>";

const char* kBadRequestPage =
    "<!DOCTYPE html><html><body><h2>Missing SSID</h2>"
    "<p><a href=\"/\">Go back</a> and fill in the form.</p></body></html>";

}  // namespace

ProvisioningServer::ProvisioningServer(WifiHandler& wifi) : wifi_(wifi)
{
}

ProvisioningServer::~ProvisioningServer()
{
    stop();
}

esp_err_t ProvisioningServer::registerHandler(const char* uri, httpd_method_t method,
                                               esp_err_t (*handler)(httpd_req_t*))
{
    httpd_uri_t def = { .uri = uri, .method = method, .handler = handler, .user_ctx = this };
    return httpd_register_uri_handler(server_, &def);
}

esp_err_t ProvisioningServer::start()
{
    if (server_) return ESP_OK;  // already running

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&server_, &config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        server_ = nullptr;
        return err;
    }

    if (err == ESP_OK) err = registerHandler("/", HTTP_GET, &ProvisioningServer::rootGetHandler);
    if (err == ESP_OK) err = registerHandler("/save", HTTP_POST, &ProvisioningServer::saveCredsPostHandler);
    if (err == ESP_OK) err = registerHandler("/status", HTTP_GET, &ProvisioningServer::statusGetHandler);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uri registration failed: %s", esp_err_to_name(err));
        stop();
        return err;
    }

    ESP_LOGI(TAG, "Provisioning server started.");
    return ESP_OK;
}

esp_err_t ProvisioningServer::stop()
{
    if (!server_) return ESP_OK;

    esp_err_t err = httpd_stop(server_);
    server_ = nullptr;
    return err;
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
        case ConnectStatus::Connected:  return sendPage(req, kSuccessPage);
        case ConnectStatus::Failed:     return sendPage(req, kConnectFailedPage);
        case ConnectStatus::Connecting:
        case ConnectStatus::Idle:       return sendPage(req, kConnectingPage);
    }
    return sendPage(req, kConnectingPage);  // unreachable; keeps -Werror happy
}

esp_err_t ProvisioningServer::saveCredsPostHandler(httpd_req_t* req)
{
    auto* self = static_cast<ProvisioningServer*>(req->user_ctx);
    return self->handleSaveCreds(req);
}

void ProvisioningServer::connectTaskEntry(void* arg)
{
    auto* self = static_cast<ProvisioningServer*>(arg);

    esp_err_t err = self->wifi_.startStation(CONNECT_TIMEOUT);
    bool connected = (err == ESP_OK);
    if (!connected)
    {
        ESP_LOGW(TAG, "station connect failed: %s", esp_err_to_name(err));
    }

    self->status_.store(connected ? ConnectStatus::Connected : ConnectStatus::Failed);
    if (self->credsSavedCb_) self->credsSavedCb_(connected);

    vTaskDelete(nullptr);
}

esp_err_t ProvisioningServer::handleSaveCreds(httpd_req_t* req)
{
    std::string body;
    if (readRequestBody(req, body, MAX_BODY_LEN) != ESP_OK)
    {
        return sendPage(req, kBadRequestPage, "400 Bad Request");
    }

    std::string ssid, password;
    if (!extractFormField(body, "ssid", kSsidFieldBufLen, ssid))
    {
        ESP_LOGW(TAG, "missing or oversized ssid field");
        return sendPage(req, kBadRequestPage, "400 Bad Request");
    }
    extractFormField(body, "password", kPasswordFieldBufLen, password);  // optional: open networks have none

    if (wifi_.saveCreds(ssid, password) != ESP_OK)
    {
        ESP_LOGE(TAG, "saveCreds rejected the submitted ssid/password");
        return sendPage(req, kBadRequestPage, "400 Bad Request");
    }

    // Hand the connection attempt off to its own task and respond instantly
    status_.store(ConnectStatus::Connecting);
    BaseType_t created = xTaskCreate(&ProvisioningServer::connectTaskEntry, "wifi_connect",
                                      4096, this, tskIDLE_PRIORITY + 1, nullptr);
    if (created != pdPASS)
    {
        ESP_LOGE(TAG, "failed to start connect task");
        status_.store(ConnectStatus::Failed);
        return sendPage(req, kConnectFailedPage);
    }

    return sendPage(req, kRedirectToStatusPage);
}