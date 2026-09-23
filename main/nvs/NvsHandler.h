//
// Created by jani on 8/19/26.
//

#ifndef SAMD_NVSHANDLER_H
#define SAMD_NVSHANDLER_H

#pragma once
#include <string>
#include "esp_err.h"

class NvsHandler
{
public:
    esp_err_t init();
    esp_err_t setString(const char* ns, const char* key, const std::string& value);
    esp_err_t getString(const char* ns, const char* key, std::string& value);
    esp_err_t eraseNamespace(const char* ns);
};

#endif //SAMD_NVSHANDLER_H