idf_component_register(
        SRCS
        "main.cpp"
        "mqtt/MQTTHandler.cpp"
        "mqtt/MQTTHandler.h"
        INCLUDE_DIRS "."
        REQUIRES mqtt
)
