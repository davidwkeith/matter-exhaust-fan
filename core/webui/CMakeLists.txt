idf_component_register(SRCS "webui.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_server settings provisioning app_update
                                esp_partition)
