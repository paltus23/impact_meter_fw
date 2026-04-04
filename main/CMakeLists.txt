idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES  data_storage battery_measure led accel http_server wifi settings rtc_server
)
