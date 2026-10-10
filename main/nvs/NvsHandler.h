#pragma once
#include <string>
#include "esp_err.h"

// probably the simplest thing to implement in the entire project.
// just init, set & get and erase.
// god i wish everything was as simple as this.

class NvsHandler
{
public:
    esp_err_t init();
    esp_err_t setString(const char* ns, const char* key, const std::string& value);
    esp_err_t getString(const char* ns, const char* key, std::string& value);   // value up to 127 chars
    esp_err_t eraseNamespace(const char* ns);
};