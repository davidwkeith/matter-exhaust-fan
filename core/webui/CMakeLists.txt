idf_component_register(SRCS "webui.c"
                       INCLUDE_DIRS "include"
                       EMBED_TXTFILES "pages/index.html"
                       REQUIRES esp_http_server settings)
