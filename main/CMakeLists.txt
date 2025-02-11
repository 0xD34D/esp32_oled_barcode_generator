idf_component_register(SRCS "u8g2_esp32_hal.c" "main.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES console driver esp_psram freertos)