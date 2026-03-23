idf_component_register(
    SRCS "accel.c"
         "adxl375.c"
    INCLUDE_DIRS "."
    REQUIRES driver freertos
)
