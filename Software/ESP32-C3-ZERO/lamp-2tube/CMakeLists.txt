cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lamp-2tube)

# Workaround: led_strip is missing #include "esp_heap_caps.h"
idf_component_get_property(led_strip_lib espressif__led_strip COMPONENT_LIB)
target_compile_options(${led_strip_lib} PRIVATE
    -include $ENV{IDF_PATH}/components/heap/include/esp_heap_caps.h)