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

# esp-matter integration, following the pattern of esp-matter's own examples:
# its component directories join EXTRA_COMPONENT_DIRS — its CMakeLists must
# NOT be included directly (it needs the IDF build system already loaded).
if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR
            "Please set ESP_MATTER_PATH to a checkout of "
            "https://github.com/espressif/esp-matter (with export.sh sourced)")
endif()
set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components"
    "${CMAKE_CURRENT_SOURCE_DIR}/core")

project(matter_exhaust_fan)

# Build-wide flags required by CHIP, exactly as esp-matter's example apps set
# them: every C++ TU that touches CHIP headers (esp_matter and dependents)
# needs gnu++17 and the generated CHIP config pulled in.
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
