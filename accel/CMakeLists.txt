idf_component_register(
    SRCS "accel.c"
         "adxl375.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES data_storage utils driver freertos settings
)
