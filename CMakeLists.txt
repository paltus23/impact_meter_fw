# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Components next to `main/` must be listed explicitly: the project root has a
# CMakeLists.txt, so IDF would treat the whole tree as one component if we only
# set EXTRA_COMPONENT_DIRS to CMAKE_CURRENT_LIST_DIR (see __project_component_dir
# in esp-idf/tools/cmake/project.cmake).
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/accel"
    "${CMAKE_CURRENT_LIST_DIR}/battery_measure"
    "${CMAKE_CURRENT_LIST_DIR}/data_storage"
    "${CMAKE_CURRENT_LIST_DIR}/http_server"
    "${CMAKE_CURRENT_LIST_DIR}/led"
    "${CMAKE_CURRENT_LIST_DIR}/settings"
    "${CMAKE_CURRENT_LIST_DIR}/wifi"
    "${CMAKE_CURRENT_LIST_DIR}/rtc_server"
    "${CMAKE_CURRENT_LIST_DIR}/utils"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(impact_meter)

idf_build_set_property(COMPILE_OPTIONS "-Wno-format-truncation" APPEND)