idf_component_register(
    SRCS "http_server.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_server data_storage settings wifi json freertos
    EMBED_TXTFILES
        "../web_pages/accelerometer_visualizer.html"
        "../web_pages/wifi_setup.html"
)
