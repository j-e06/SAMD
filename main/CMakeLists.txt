idf_component_register(
        SRCS
        "main.cpp"
        "mqtt/MQTTHandler.cpp"
        "mqtt/MQTTHandler.h"
        "wifi/WifiHandler.cpp"
        "wifi/WifiHandler.h"
        "nvs/NvsHandler.cpp"
        "nvs/NvsHandler.h"

        INCLUDE_DIRS "."
        REQUIRES espressif__mqtt esp_wifi nvs_flash
        PRIV_REQUIRES esp_driver_gpio
)
