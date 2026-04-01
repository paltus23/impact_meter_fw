idf_component_register(
    SRCS "http_server.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_server data_storage settings wifi json freertos
    EMBED_TXTFILES "../visualizer/accelerometer_visualizer.html"
)
