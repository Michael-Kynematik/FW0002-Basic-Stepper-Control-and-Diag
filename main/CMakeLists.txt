set(FW_VERSION "0.0.0")
if(EXISTS "${CMAKE_SOURCE_DIR}/VERSION")
    file(READ "${CMAKE_SOURCE_DIR}/VERSION" FW_VERSION)
    string(STRIP "${FW_VERSION}" FW_VERSION)
endif()

set(FW_BUILD "unknown")
execute_process(
    COMMAND git -C ${CMAKE_SOURCE_DIR} rev-parse --short HEAD
    OUTPUT_VARIABLE GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(GIT_SHA)
    set(FW_BUILD "${GIT_SHA}")
    execute_process(
        COMMAND git -C ${CMAKE_SOURCE_DIR} status --porcelain
        OUTPUT_VARIABLE GIT_STATUS
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(GIT_STATUS)
        set(FW_BUILD "${FW_BUILD}-dirty")
    endif()
endif()

set(SNAPSHOT_SCHEMA_VERSION 1)
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/include/fw_version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/fw_version.h"
    @ONLY
)

idf_component_register(
    SRCS "stepper_driver_uart.c" "motor.c" "ir_sensor.c" "ir_emitter.c" "neopixel_strip.c" "neopixel.c" "loadcell_scale.c" "loadcell_adc.c" "app_main.c" "diag_console.c" "snapshot.c" "events.c" "remote_actions.c" "board.c" "json_helpers.c"
    INCLUDE_DIRS "include" "${CMAKE_CURRENT_BINARY_DIR}"
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_RMT_SUPPRESS_DEPRECATE_WARN=1)

