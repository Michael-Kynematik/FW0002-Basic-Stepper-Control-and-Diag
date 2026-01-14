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
    SRCS "app_main.c" "diag_console.c" "snapshot.c" "events.c" "remote_actions.c"
    INCLUDE_DIRS "include" "${CMAKE_CURRENT_BINARY_DIR}"
)
