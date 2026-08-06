cmake_minimum_required(VERSION 3.16)

# Board selection: -DBOARD=<name> picks boards/<name>/board-config.yaml.
# The YAML is compiled to main/board_config.h at configure time so that core
# code never contains board-specific values.
if(NOT DEFINED BOARD)
    set(BOARD "generic-2gang")
endif()
set(BOARD_CONFIG_YAML "${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD}/board-config.yaml")
if(NOT EXISTS ${BOARD_CONFIG_YAML})
    message(FATAL_ERROR "Unknown board '${BOARD}': ${BOARD_CONFIG_YAML} not found")
endif()
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env python3
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_board_config.py
            ${BOARD_CONFIG_YAML}
            ${CMAKE_CURRENT_SOURCE_DIR}/main/board_config.h
    RESULT_VARIABLE GEN_BOARD_CONFIG_RESULT
)
if(NOT GEN_BOARD_CONFIG_RESULT EQUAL 0)
    message(FATAL_ERROR "board-config generation failed for board '${BOARD}'")
endif()

# esp-matter is required for the Matter component. Point ESP_MATTER_PATH at a
# checkout of https://github.com/espressif/esp-matter before building.
if(DEFINED ENV{ESP_MATTER_PATH})
    include($ENV{ESP_MATTER_PATH}/CMakeLists.txt OPTIONAL)
endif()

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/core")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(matter_exhaust_fan)
