idf_component_register(SRCS "hostname.c" "wifi_maintenance.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_netif esp_wifi esp_event efuse esp_system
                                settings)
