idf_component_register(SRCS "iaq_history.c"
                       INCLUDE_DIRS "include"
                       REQUIRES freertos iaq_data time_sync)
