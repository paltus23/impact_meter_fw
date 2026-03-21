idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES  data_storage battery_measure led accel http_server wifi
)
