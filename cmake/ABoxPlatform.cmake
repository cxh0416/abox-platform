function(abox_platform_verify_registry target)
    cmake_parse_arguments(ABOX_REGISTRY "LEGACY" "REGISTRY" "" ${ARGN})
    if(ABOX_REGISTRY_UNPARSED_ARGUMENTS OR ABOX_REGISTRY_KEYWORDS_MISSING_VALUES OR
            NOT ABOX_REGISTRY_REGISTRY OR NOT TARGET ${target})
        message(FATAL_ERROR "abox_platform_verify_registry: host target and REGISTRY are required")
    endif()
    get_target_property(registry_target_type ${target} TYPE)
    if(CMAKE_CROSSCOMPILING OR NOT registry_target_type STREQUAL "EXECUTABLE")
        message(FATAL_ERROR "abox_platform_verify_registry: a host executable is required")
    endif()
    if(NOT BUILD_TESTING)
        message(FATAL_ERROR "abox_platform_verify_registry: BUILD_TESTING must be enabled")
    endif()
    get_filename_component(registry_json "${ABOX_REGISTRY_REGISTRY}" ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT EXISTS "${registry_json}")
        message(FATAL_ERROR "abox_platform_verify_registry: Registry file does not exist: ${registry_json}")
    endif()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(registry_mode --require-complete)
    if(ABOX_REGISTRY_LEGACY)
        set(registry_mode)
    endif()
    add_test(NAME ${target}_registry COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/verify_profile_descriptor.py"
        "${registry_json}" "$<TARGET_FILE:${target}>" ${registry_mode})
endfunction()

function(abox_platform_attach_components target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "abox_platform_attach_components: target '${target}' does not exist")
    endif()
    cmake_parse_arguments(ABOX "" "SCHEDULER;HARDWARE" "COMPONENTS" ${ARGN})
    if(ABOX_UNPARSED_ARGUMENTS OR ABOX_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "abox_platform_attach_components: invalid arguments")
    endif()
    if(NOT ABOX_SCHEDULER MATCHES "^(BAREMETAL|FREERTOS)$")
        message(FATAL_ERROR "abox_platform_attach_components: SCHEDULER must be BAREMETAL or FREERTOS")
    endif()
    if(NOT ABOX_COMPONENTS)
        message(FATAL_ERROR "abox_platform_attach_components: COMPONENTS is required")
    endif()
    if(ABOX_HARDWARE)
        set(hardware_contract "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../targets/${ABOX_HARDWARE}/HardwareContract.cmake")
        if(NOT EXISTS "${hardware_contract}")
            message(FATAL_ERROR "abox_platform_attach_components: unknown HARDWARE '${ABOX_HARDWARE}'")
        endif()
        include("${hardware_contract}")
        abox_hardware_validate(${target} "${ABOX_PRODUCT_CONFIG_DIR}" "${ABOX_SCHEDULER}")
    endif()
    string(TOLOWER "${ABOX_SCHEDULER}" scheduler_target)
    target_link_libraries(${target} abox::port_${scheduler_target})
    foreach(component IN LISTS ABOX_COMPONENTS)
        if(component STREQUAL "boot" OR component STREQUAL "ota")
            message(FATAL_ERROR "abox_platform_attach_components: legacy Boot/OTA must be attached explicitly")
        endif()
        if(NOT component MATCHES "^(core|board_io|cjson|ec_power|ec800|ec800_ufs|ec800_command_port|ec800_tls|mqtt_tls|mqtt_runtime|mqtt_trial|mqtt_v4|mqtt_ec800_rx|mqtt_ec800|enrollment|enrollment_ec800_http|boot_v2_common|boot_v2_app|https_ufs_downloader|cloud_service|log_ring|maintenance_service|app_service|mqtt_digest|mqtt_request|mqtt_idempotency|mqtt_backoff|mqtt_registry|ec800_ufs_at|ufs_journal|cloud_v4|mqtt_idem_ufs)$")
            message(FATAL_ERROR "abox_platform_attach_components: unknown component '${component}'")
        endif()
        if(NOT TARGET abox::${component})
            message(FATAL_ERROR "abox_platform_attach_components: unavailable component '${component}'")
        endif()
        target_link_libraries(${target} abox::${component})
    endforeach()
    foreach(requirement IN ITEMS
            "ec_power:core" "ec800_command_port:ec800" "ec800_tls:ec800_ufs"
            "mqtt_tls:ec800_tls" "mqtt_runtime:mqtt_tls"
            "mqtt_runtime:ec800_command_port" "mqtt_ec800:ec800"
            "mqtt_ec800:mqtt_ec800_rx" "enrollment:cjson"
            "cloud_service:cjson"
            "mqtt_digest:mqtt_v4" "mqtt_digest:cjson" "mqtt_request:mqtt_digest"
            "mqtt_idempotency:mqtt_request" "ec800_ufs_at:ec800" "ec800_ufs_at:ec800_ufs"
            "mqtt_registry:mqtt_request"
            "ufs_journal:ec800_ufs" "ufs_journal:boot_v2_common"
            "cloud_v4:cloud_service" "cloud_v4:mqtt_request"
            "mqtt_idem_ufs:mqtt_idempotency" "mqtt_idem_ufs:ufs_journal"
            "enrollment_ec800_http:ec800" "boot_v2_common:core"
            "boot_v2_app:boot_v2_common" "boot_v2_app:https_ufs_downloader")
        string(REPLACE ":" ";" pair "${requirement}")
        list(GET pair 0 selected)
        list(GET pair 1 dependency)
        if(selected IN_LIST ABOX_COMPONENTS AND NOT dependency IN_LIST ABOX_COMPONENTS)
            message(FATAL_ERROR
                "abox_platform_attach_components: '${selected}' requires '${dependency}'")
        endif()
    endforeach()
endfunction()

# Opt-in standard App HAL/Flash/EC800 power port. Products with a different
# board or Flash layout continue to supply their own ABoxPlatformPort.
function(abox_platform_attach_standard_port target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "abox_platform_attach_standard_port: target '${target}' does not exist")
    endif()
    cmake_parse_arguments(ABOX_PORT "" "HARDWARE" "" ${ARGN})
    if(ABOX_PORT_UNPARSED_ARGUMENTS OR ABOX_PORT_KEYWORDS_MISSING_VALUES OR
            NOT ABOX_PORT_HARDWARE STREQUAL "stm32f105_ec800_v1")
        message(FATAL_ERROR "abox_platform_attach_standard_port: HARDWARE must be stm32f105_ec800_v1")
    endif()
    target_sources(${target} PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../targets/stm32f105_ec800_v1/platform_port_stm32f105.c")
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../targets/stm32f105_ec800_v1/include")
endfunction()

# Standard board link set for a new App. This does not create the product's
# startup, control/safety task or protocol handlers; it keeps one explicit
# scheduler and hardware contract while reusing the common communication stack.
function(abox_platform_attach_standard_clock target)
    target_sources(${target} PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../targets/stm32f105_ec800_v1/monotonic_stm32f105.c")
    target_compile_definitions(${target} PRIVATE ABOX_STM32_MONOTONIC=1)
endfunction()

function(abox_platform_attach_standard_app target)
    cmake_parse_arguments(ABOX_APP "" "PRODUCT_CONFIG_DIR;SCHEDULER" "" ${ARGN})
    if(ABOX_APP_UNPARSED_ARGUMENTS OR ABOX_APP_KEYWORDS_MISSING_VALUES OR
            NOT ABOX_APP_PRODUCT_CONFIG_DIR OR NOT ABOX_APP_SCHEDULER)
        message(FATAL_ERROR "abox_platform_attach_standard_app: PRODUCT_CONFIG_DIR and SCHEDULER are required")
    endif()
    set(ABOX_PRODUCT_CONFIG_DIR "${ABOX_APP_PRODUCT_CONFIG_DIR}")
    abox_platform_attach_components(${target}
        SCHEDULER ${ABOX_APP_SCHEDULER}
        HARDWARE stm32f105_ec800_v1
        COMPONENTS core cjson board_io ec_power ec800 ec800_ufs
                   ec800_command_port ec800_tls mqtt_tls mqtt_runtime
                   mqtt_trial mqtt_v4 mqtt_ec800_rx mqtt_ec800
                   enrollment enrollment_ec800_http https_ufs_downloader
                   boot_v2_common boot_v2_app cloud_service log_ring maintenance_service app_service)
    option(ABOX_PSK_STATION "Controlled local SWD PSK injection; OFF in ordinary firmware" OFF)
    if(ABOX_PSK_STATION)
        target_sources(${target} PRIVATE
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../components/ec800/src/abox_psk_station.c")
        target_compile_definitions(${target} PRIVATE ABOX_PSK_STATION=1)
    endif()
    abox_platform_attach_standard_port(${target} HARDWARE stm32f105_ec800_v1)
    target_sources(${target} PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../targets/stm32f105_ec800_v1/enrollment_identity_stm32f105.c")
endfunction()

function(abox_platform_attach target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "abox_platform_attach: target '${target}' does not exist")
    endif()

    target_link_libraries(${target} abox::core)
    target_link_libraries(${target} abox::ec_power)
    if(NOT DEFINED ABOX_PLATFORM_USE_CJSON OR ABOX_PLATFORM_USE_CJSON)
        target_link_libraries(${target} abox::cjson)
    endif()

    if(ABOX_PLATFORM_SCHEDULER STREQUAL "FREERTOS")
        target_link_libraries(${target} abox::port_freertos)
    else()
        target_link_libraries(${target} abox::port_baremetal)
    endif()

    if(ABOX_PRODUCT_CONFIG_DIR)
        target_include_directories(${target} PRIVATE "${ABOX_PRODUCT_CONFIG_DIR}")
    endif()
endfunction()

function(abox_platform_attach_mqtt target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "abox_platform_attach_mqtt: target '${target}' does not exist")
    endif()
    cmake_parse_arguments(ABOX_MQTT "" "VERSION" "" ${ARGN})
    if(NOT ABOX_MQTT_VERSION STREQUAL "4")
        message(FATAL_ERROR "abox_platform_attach_mqtt currently supports VERSION 4")
    endif()
    target_link_libraries(${target} abox::mqtt_v4 abox::ec800)
endfunction()

function(abox_platform_attach_boot target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "abox_platform_attach_boot: target '${target}' does not exist")
    endif()

    target_link_libraries(${target} abox::boot abox::ota abox::ec_power)

    if(ABOX_PRODUCT_CONFIG_DIR)
        target_include_directories(${target} PRIVATE "${ABOX_PRODUCT_CONFIG_DIR}")
    endif()
endfunction()

function(abox_platform_attach_boot_v2_app target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "abox_platform_attach_boot_v2_app: target '${target}' does not exist")
    endif()
    target_link_libraries(${target} abox::boot_v2_app)
    if(ABOX_PRODUCT_CONFIG_DIR)
        target_include_directories(${target} PRIVATE "${ABOX_PRODUCT_CONFIG_DIR}")
    endif()
endfunction()

function(abox_platform_attach_boot_v2 target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "abox_platform_attach_boot_v2: target '${target}' does not exist")
    endif()
    target_link_libraries(${target} abox::boot_v2_boot abox::ec_power)
    if(ABOX_PRODUCT_CONFIG_DIR)
        target_include_directories(${target} PRIVATE "${ABOX_PRODUCT_CONFIG_DIR}")
    endif()
endfunction()
