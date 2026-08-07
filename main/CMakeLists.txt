# board_config.h is generated into this directory by tools/gen_board_config.py
# (driven from the top-level CMakeLists.txt via -DBOARD=<name>).
idf_component_register(SRCS "app_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES input dimmer mua settings matter provisioning
                                webui driver app_update)
