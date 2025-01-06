# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.0)
set(toolchainVersion 13_3_Rel1)
set(picotoolVersion 2.1.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ====================================================================================

set(PICO_BOARD pico2_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(picow_httpd_background C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()


add_executable(picow_httpd_background
        pico_httpd.c
        )

pico_set_program_name(picow_httpd_background "picow_httpd_background")
pico_set_program_version(picow_httpd_background "0.1")

target_compile_definitions(picow_httpd_background PRIVATE
        WIFI_SSID=\"na3ou25_1\"
        WIFI_PASSWORD=\"AXL@dia6632\"
        )
target_include_directories(picow_httpd_background PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${PICO_LWIP_CONTRIB_PATH}/apps/httpd
        )

target_compile_options(picow_httpd_background PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/custom.h)

target_link_libraries(picow_httpd_background PRIVATE
        pico_cyw43_arch_lwip_threadsafe_background
        pico_lwip_http
        pico_lwip_mdns
        pico_httpd_content
        pico_stdlib
        )

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(picow_httpd_background 0)
pico_enable_stdio_usb(picow_httpd_background 1)

pico_add_extra_outputs(picow_httpd_background)

pico_add_library(pico_httpd_content NOFLAG)
#pico_set_lwip_httpd_content(picow_httpd_background ${CMAKE_CURRENT_LIST_DIR}/content)

pico_set_lwip_httpd_content(pico_httpd_content INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/content/404.html
        ${CMAKE_CURRENT_LIST_DIR}/content/index.shtml
        ${CMAKE_CURRENT_LIST_DIR}/content/test.shtml
        ${CMAKE_CURRENT_LIST_DIR}/content/ledpass.shtml
        ${CMAKE_CURRENT_LIST_DIR}/content/ledfail.shtml
        ${CMAKE_CURRENT_LIST_DIR}/content/img/rpi.png
        )