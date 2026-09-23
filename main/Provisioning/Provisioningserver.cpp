#include "Provisioningserver.h"
#include "esp_log.h"
#include <cstring>
#include <cstdlib>
#include <string>

static const char* TAG = "ProvisioningServer";

namespace {

// Minimal decoder for application/x-www-form-urlencoded values:
// '+' -> space, "%XX" -> the byte it encodes. Anything malformed is
// passed through literally rather than rejected outright.
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

const char* kFormPage =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<title>Wi-Fi setup</title></head><body>"
    "<h2>Wi-Fi setup</h2>"
    "<form method=\"POST\" action=\"/save\">"
    "SSID:<br><input name=\"ssid\" maxlength=\"32\"><br>"
    "Password:<br><input name=\"password\" type=\"password\" maxlength=\"64\"><br><br>"
    "<button type=\"submit\">Save</button>"
    "</form></body></html>";

const char* kSavedPage =
    "<!DOCTYPE html><html><body><h2>Saved.</h2>"
    "<p>The device will try to connect to the network now.</p></body></html>";

const char* kErrorPage =
    "<!DOCTYPE html><html><body><h2>Error</h2>"
    "<p>Could not save credentials. Go back and try again.</p></body></html>";

}  // namespace

ProvisioningServer::ProvisioningServer(WifiHandler& wifi) : wifi_(wifi)
{
}

ProvisioningServer::~ProvisioningServer()
{
    stop();
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

    httpd_uri_t rootUri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = &ProvisioningServer::rootGetHandler,
        .user_ctx = this,
    };
    err = httpd_register_uri_handler(server_, &rootUri);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "register / failed: %s", esp_err_to_name(err));
        stop();
        return err;
    }

    httpd_uri_t saveUri = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = &ProvisioningServer::saveCredsPostHandler,
        .user_ctx = this,
    };
    err = httpd_register_uri_handler(server_, &saveUri);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "register /save failed: %s", esp_err_to_name(err));
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
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, kFormPage, HTTPD_RESP_USE_STRLEN);
}

esp_err_t ProvisioningServer::saveCredsPostHandler(httpd_req_t* req)
{
    auto* self = static_cast<ProvisioningServer*>(req->user_ctx);
    esp_err_t err = self->handleSaveCreds(req);
    if (err != ESP_OK)
    {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, kErrorPage, HTTPD_RESP_USE_STRLEN);
    }
    // Response has been sent either way (success page or error page above);
    // returning an esp_err_t here would make httpd close the connection abruptly.
    return ESP_OK;
}

esp_err_t ProvisioningServer::handleSaveCreds(httpd_req_t* req)
{
    if (req->content_len == 0 || req->content_len > MAX_BODY_LEN)
    {
        ESP_LOGW(TAG, "bad content_len: %d", static_cast<int>(req->content_len));
        return ESP_ERR_INVALID_SIZE;
    }

    std::string body;
    body.resize(req->content_len);

    size_t received = 0;
    while (received < req->content_len)
    {
        int ret = httpd_req_recv(req, &body[received], req->content_len - received);
        if (ret <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ESP_LOGW(TAG, "httpd_req_recv failed: %d", ret);
            return ESP_FAIL;
        }
        received += ret;
    }

    // Encoded fields can be up to ~3x the decoded length (each special byte -> "%XX"),
    // so these buffers are sized generously; the decoded ssid/password are what
    // actually get length-checked, by WifiHandler::saveCreds().
    char ssidRaw[128] = {};
    char passRaw[256] = {};

    esp_err_t err = httpd_query_key_value(body.c_str(), "ssid", ssidRaw, sizeof(ssidRaw));
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "missing or oversized ssid field: %s", esp_err_to_name(err));
        return ESP_ERR_INVALID_ARG;
    }

    // password is optional (open network case) -- don't fail if it's absent,
    // but do fail if it's present and simply too long for the buffer.
    err = httpd_query_key_value(body.c_str(), "password", passRaw, sizeof(passRaw));
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND)
    {
        ESP_LOGW(TAG, "oversized password field: %s", esp_err_to_name(err));
        return ESP_ERR_INVALID_ARG;
    }

    std::string ssid = urlDecode(ssidRaw, strlen(ssidRaw));
    std::string password = urlDecode(passRaw, strlen(passRaw));

    err = wifi_.saveCreds(ssid, password);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "saveCreds failed: %s", esp_err_to_name(err));
        return err;
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, kSavedPage, HTTPD_RESP_USE_STRLEN);

    if (credsSavedCb_) credsSavedCb_();
    return ESP_OK;
}