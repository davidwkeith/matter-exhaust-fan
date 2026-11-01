# Raspberry Pi 3 controller (exhaustd) — Linux counterpart of the ESP-IDF
# build at the repo root. Reuses the portable core modules unchanged.
#
#   cmake -S linux -B build-linux [-DBOARD=rpi3-2gang] && cmake --build build-linux
cmake_minimum_required(VERSION 3.16)
project(exhaustd C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(NOT DEFINED BOARD)
    set(BOARD "rpi3-2gang")
endif()
set(BOARD_CONFIG_YAML "${REPO_ROOT}/boards/${BOARD}/board-config.yaml")
if(NOT EXISTS ${BOARD_CONFIG_YAML})
    message(FATAL_ERROR "Unknown board '${BOARD}': ${BOARD_CONFIG_YAML} not found")
endif()
set(BOARD_CONFIG_H ${CMAKE_CURRENT_BINARY_DIR}/generated/board_config.h)
add_custom_command(
    OUTPUT ${BOARD_CONFIG_H}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
    COMMAND python3 ${REPO_ROOT}/tools/gen_board_config.py ${BOARD_CONFIG_YAML} ${BOARD_CONFIG_H}
    DEPENDS ${BOARD_CONFIG_YAML} ${REPO_ROOT}/tools/gen_board_config.py
    COMMENT "Generating board_config.h for ${BOARD}"
)

add_executable(exhaustd
    ${BOARD_CONFIG_H}
    src/main.c
    src/gpio.c
    src/bridge.c
    src/settings.c
    src/hostname.c
    src/sd_notify.c
    src/matter_stub.c
    ${REPO_ROOT}/core/input/rocker.c
    ${REPO_ROOT}/core/input/channel.c
    ${REPO_ROOT}/core/input/reset_chord.c
    ${REPO_ROOT}/core/dimmer/dimmer_curve.c
    ${REPO_ROOT}/core/dimmer/ramp.c
    ${REPO_ROOT}/core/mua/mua.c
)
target_include_directories(exhaustd PRIVATE
    src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${REPO_ROOT}/core/input/include
    ${REPO_ROOT}/core/dimmer/include
    ${REPO_ROOT}/core/mua/include
)
target_compile_options(exhaustd PRIVATE -Wall -Wextra -Werror)

install(TARGETS exhaustd RUNTIME DESTINATION sbin)
install(FILES exhaustd.service DESTINATION lib/systemd/system)
