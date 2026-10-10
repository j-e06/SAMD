idf_component_register(
        SRCS
        "main.cpp"
        "mqtt/MQTTHandler.cpp"
        "wifi/WifiHandler.cpp"
        "nvs/NvsHandler.cpp"
        "NTC/NTC.cpp"
        "MAX30102/MAX30102.cpp"
        "pump/pump.cpp"
        "processing/Processing.cpp"
        "Provisioning/Provisioningserver.cpp"
        "buttons/Buttons.cpp"
        INCLUDE_DIRS "."
        REQUIRES espressif__mqtt esp_wifi nvs_flash esp_http_server
        PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_adc esp_timer esp_event esp_netif esp-tls
)
# broker ca (pem) for mqtts://, if we don't do this the built in certificate is used
# which wouldn't work.
# goes to mqtt if fails ig
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/certs/broker_ca.pem")
    target_add_binary_data(${COMPONENT_LIB} "certs/broker_ca.pem" TEXT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE SAMD_HAVE_BROKER_CA=1)
endif()