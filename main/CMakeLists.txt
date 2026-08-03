# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/main/*.*)
idf_component_register(SRCS ${app_sources}
                    INCLUDE_DIRS "."
                    REQUIRES
                        esp_driver_ledc
                        freertos
                        led_strip
                        lvgl
                        esp_lcd_gc9a01
                        esp_lvgl_port
                        esp_adc
                        esp_timer
                        nvs_flash
                        esp-zigbee-lib
                        esp-zboss-lib
                    )