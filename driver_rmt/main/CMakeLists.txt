idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES rmt_raw driver esp_driver_rmt)