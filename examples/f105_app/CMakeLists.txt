cmake_minimum_required(VERSION 3.22)
project(ABoxF105Example LANGUAGES C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(ABOX_HARDWARE "" CACHE STRING "Explicit platform hardware target")
option(ABOX_EXAMPLE_CAN "Compile the public CAN silent-loopback example" ON)
option(ABOX_EXAMPLE_CAN_EVENTS "Compile the public CAN event service" OFF)
if(NOT ABOX_HARDWARE OR NOT ABOX_BOARD_VERSION)
    message(FATAL_ERROR "Set ABOX_HARDWARE and ABOX_BOARD_VERSION explicitly")
endif()
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../.." abox_platform_build)
add_executable(ABoxF105Example main.c syscalls.c sysmem.c)
target_include_directories(ABoxF105Example PRIVATE "${CMAKE_CURRENT_LIST_DIR}/config")
target_link_options(ABoxF105Example PRIVATE
    "-T${CMAKE_CURRENT_LIST_DIR}/f105_app.ld"
    "--specs=nano.specs" "-Wl,--gc-sections")
set_property(TARGET ABoxF105Example APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/f105_app.ld")
set(example_can)
if(ABOX_EXAMPLE_CAN)
    set(example_can CAN)
endif()
if(ABOX_EXAMPLE_CAN_EVENTS)
    list(APPEND example_can CAN_EVENTS)
endif()
abox_platform_attach_f105_app(ABoxF105Example
    HARDWARE "${ABOX_HARDWARE}"
    PRODUCT_CONFIG_DIR "${CMAKE_CURRENT_LIST_DIR}/config"
    ${example_can})
