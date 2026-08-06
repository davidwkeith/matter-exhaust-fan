idf_component_register(SRCS "hostname.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_netif efuse esp_system)
