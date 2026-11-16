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
        if(NOT component MATCHES "^(core|board_io|cjson|ec_power|ec800|ec800_ufs|ec800_command_port|ec800_tls|mqtt_tls|mqtt_runtime|mqtt_trial|mqtt_v4|mqtt_ec800_rx|mqtt_ec800|enrollment|enrollment_ec800_http|boot_v2_common|boot_v2_app|https_ufs_downloader|cloud_service|log_ring|rtt|rtt_console|maintenance_service|app_service|mqtt_digest|mqtt_request|mqtt_idempotency|mqtt_backoff|mqtt_registry|ec800_ufs_at|ufs_journal|cloud_v4|mqtt_idem_ufs)$")
            message(FATAL_ERROR "abox_platform_attach_components: unknown component '${component}'")
        endif()
        if(NOT TARGET abox::${component})
            message(FATAL_ERROR "abox_platform_attach_components: unavailable component '${component}'")
        endif()
        target_link_libraries(${target} abox::${component})
    endforeach()
    foreach(requirement IN ITEMS
            "ec_power:core" "ec800_command_port:ec800" "ec800_tls:ec800_ufs"
            "rtt_console:rtt" "rtt_console:ec800"
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

# Fixed STM32F105 vendor sources; the consumer supplies stm32f1xx_hal_conf.h,
# startup and system clock initialization. Do not also link a second HAL copy.
function(abox_platform_attach_stm32f105_vendor target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "abox_platform_attach_stm32f105_vendor: target '${target}' does not exist")
    endif()
    set(vendor "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../vendor/stm32f1/hal-1.1.10")
    target_include_directories(${target} PUBLIC
        "${vendor}/STM32F1xx_HAL_Driver/Inc"
        "${vendor}/STM32F1xx_HAL_Driver/Inc/Legacy"
        "${vendor}/CMSIS/Device/ST/STM32F1xx/Include"
        "${vendor}/CMSIS/Include")
    target_compile_definitions(${target} PUBLIC USE_HAL_DRIVER STM32F105xC)
    foreach(driver IN ITEMS gpio_ex tim tim_ex can rcc rcc_ex gpio dma cortex pwr flash flash_ex exti uart)
        target_sources(${target} PRIVATE "${vendor}/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_${driver}.c")
    endforeach()
    target_sources(${target} PRIVATE "${vendor}/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal.c")
endfunction()

# ST board peripheral initialization and interrupt sources. Products provide
# stm32f1xx_hal_conf.h, syscalls, the linker script and their App main.
function(abox_platform_attach_stm32f105_board_sources target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "abox_platform_attach_stm32f105_board_sources: target does not exist")
    endif()
    set(board "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/boards/abox_v2/stm32cubemx")
    target_include_directories(${target} BEFORE PRIVATE "${board}/include")
    foreach(source IN ITEMS gpio can dma usart stm32f1xx_it stm32f1xx_hal_msp
            stm32f1xx_hal_timebase_tim system_stm32f1xx)
        target_sources(${target} PRIVATE "${board}/src/${source}.c")
    endforeach()
    target_sources(${target} PRIVATE "${board}/startup_stm32f105xc.s")
endfunction()

# Explicit board startup for products using the standard App initialization.
function(abox_platform_attach_board_startup target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "abox_platform_attach_board_startup: target does not exist")
    endif()
    cmake_parse_arguments(ABOX_BOARD "" "HARDWARE" "" ${ARGN})
    if(ABOX_BOARD_UNPARSED_ARGUMENTS OR ABOX_BOARD_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "abox_platform_attach_board_startup: invalid arguments")
    endif()
    if(ABOX_BOARD_HARDWARE STREQUAL "stm32f105_ec800_v1")
        target_sources(${target} PRIVATE
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/boards/abox_v2/board_stm32f105.c")
    elseif(NOT ABOX_BOARD_HARDWARE STREQUAL "gd32f105_ec800_v1")
        message(FATAL_ERROR "abox_platform_attach_board_startup: unsupported HARDWARE")
    endif()
    # The GD startup implementation is in board_gd32f105.c, attached by
    # abox_platform_attach_standard_port for the matching target.
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include")
endfunction()

# Polling CAN port is opt-in and owns its two controllers and FIFO0.
function(abox_platform_attach_can_port target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "abox_platform_attach_can_port: target '${target}' does not exist")
    endif()
    cmake_parse_arguments(ABOX_CAN "" "HARDWARE" "" ${ARGN})
    if(ABOX_CAN_UNPARSED_ARGUMENTS OR ABOX_CAN_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "abox_platform_attach_can_port: invalid arguments")
    endif()
    if(ABOX_CAN_HARDWARE STREQUAL "stm32f105_ec800_v1")
        set(mcu stm32f105)
    elseif(ABOX_CAN_HARDWARE STREQUAL "gd32f105_ec800_v1")
        set(mcu gd32f105)
        target_sources(${target} PRIVATE
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../vendor/gd32f10x/2.7.0/GD32F10x_standard_peripheral/Source/gd32f10x_can.c"
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../vendor/gd32f10x/2.7.0/GD32F10x_standard_peripheral/Source/gd32f10x_dbg.c")
    else()
        message(FATAL_ERROR "abox_platform_attach_can_port: unsupported HARDWARE")
    endif()
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_can_board.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/${mcu}/can_port_${mcu}.c")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/internal/include")
    # The public port owns both controllers and FIFO0. Standard STM32 board
    # startup must therefore leave the controllers for the port to initialize.
    target_compile_definitions(${target} PRIVATE ABOX_CAN_PORT_ENABLED=1)
endfunction()

# Optional mailbox ownership and completion service. It is independent of the
# polling port and uses the same controller after ABoxCanPort_Init.
function(abox_platform_attach_can_events target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "abox_platform_attach_can_events: target does not exist")
    endif()
    cmake_parse_arguments(ABOX_EVENTS "" "HARDWARE" "" ${ARGN})
    if(ABOX_EVENTS_UNPARSED_ARGUMENTS OR ABOX_EVENTS_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "abox_platform_attach_can_events: invalid arguments")
    endif()
    if(ABOX_EVENTS_HARDWARE STREQUAL "stm32f105_ec800_v1")
        set(mcu stm32f105)
    elseif(ABOX_EVENTS_HARDWARE STREQUAL "gd32f105_ec800_v1")
        set(mcu gd32f105)
    else()
        message(FATAL_ERROR "abox_platform_attach_can_events: unsupported HARDWARE")
    endif()
    target_sources(${target} PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_can_events.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/${mcu}/can_events_${mcu}.c")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include")
    target_compile_definitions(${target} PRIVATE ABOX_CAN_EVENTS_ENABLED=1)
endfunction()

function(abox_platform_attach_gd32_watchdog target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "abox_platform_attach_gd32_watchdog: target does not exist")
    endif()
    target_sources(${target} PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/gd32f105/watchdog_port_gd32f105.c")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include")
endfunction()

function(abox_platform_attach_watchdog_port target)
    cmake_parse_arguments(ABOX_WATCHDOG "" "HARDWARE" "" ${ARGN})
    if(NOT TARGET ${target} OR ABOX_WATCHDOG_UNPARSED_ARGUMENTS OR ABOX_WATCHDOG_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "abox_platform_attach_watchdog_port: invalid arguments")
    endif()
    if(ABOX_WATCHDOG_HARDWARE STREQUAL "stm32f105_ec800_v1")
        set(mcu stm32f105)
    elseif(ABOX_WATCHDOG_HARDWARE STREQUAL "gd32f105_ec800_v1")
        set(mcu gd32f105)
    else()
        message(FATAL_ERROR "abox_platform_attach_watchdog_port: unsupported HARDWARE")
    endif()
    target_sources(${target} PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/${mcu}/watchdog_port_${mcu}.c")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include")
endfunction()

function(abox_platform_attach_gpio target)
    cmake_parse_arguments(ABOX_GPIO "" "HARDWARE" "" ${ARGN})
    if(NOT TARGET ${target} OR ABOX_GPIO_UNPARSED_ARGUMENTS OR ABOX_GPIO_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "abox_platform_attach_gpio: invalid arguments")
    endif()
    if(ABOX_GPIO_HARDWARE STREQUAL "stm32f105_ec800_v1")
        set(mcu stm32f105)
    elseif(ABOX_GPIO_HARDWARE STREQUAL "gd32f105_ec800_v1")
        set(mcu gd32f105)
    else()
        message(FATAL_ERROR "abox_platform_attach_gpio: unsupported HARDWARE")
    endif()
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/${mcu}/gpio_${mcu}.c")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include")
endfunction()

# Independent CRC engine, backup data, pin remapping and PVD are opt-in.
# They do not select board pins, reset the RTC domain or enable low-power modes.
function(_abox_platform_attach_system_peripheral target module)
        string(TOUPPER "${module}" module_upper)
        cmake_parse_arguments(ABOX_MODULE "" "HARDWARE" "" ${ARGN})
        if(NOT TARGET ${target} OR ABOX_MODULE_UNPARSED_ARGUMENTS OR ABOX_MODULE_KEYWORDS_MISSING_VALUES)
            message(FATAL_ERROR "abox_platform_attach_${module}: invalid arguments")
        endif()
        get_target_property(existing ${target} ABOX_${module_upper}_HARDWARE)
        if(existing)
            if(NOT existing STREQUAL ABOX_MODULE_HARDWARE)
                message(FATAL_ERROR "abox_platform_attach_${module}: target has a different MCU")
            endif()
            return()
        endif()
        if(ABOX_MODULE_HARDWARE STREQUAL "stm32f105_ec800_v1")
            set(mcu stm32f105)
        elseif(ABOX_MODULE_HARDWARE STREQUAL "gd32f105_ec800_v1")
            set(mcu gd32f105)
        else()
            message(FATAL_ERROR "abox_platform_attach_${module}: unsupported HARDWARE")
        endif()
        target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_${module}.c"
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/${mcu}/${module}_${mcu}.c")
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include"
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/internal/include")
        if(module STREQUAL "backup")
            target_link_libraries(${target} abox::core)
        elseif(module STREQUAL "pvd" OR module STREQUAL "pinmux")
            abox_platform_attach_critical(${target} HARDWARE "${ABOX_MODULE_HARDWARE}")
        endif()
        if(module STREQUAL "pvd")
            target_compile_definitions(${target} PRIVATE ABOX_PVD_API_ENABLED=1)
        endif()
        set_property(TARGET ${target} PROPERTY ABOX_${module_upper}_HARDWARE "${ABOX_MODULE_HARDWARE}")
endfunction()
function(abox_platform_attach_crc target)
    _abox_platform_attach_system_peripheral(${target} crc ${ARGN})
endfunction()
function(abox_platform_attach_backup target)
    _abox_platform_attach_system_peripheral(${target} backup ${ARGN})
endfunction()
function(abox_platform_attach_pvd target)
    _abox_platform_attach_system_peripheral(${target} pvd ${ARGN})
endfunction()
function(abox_platform_attach_pinmux target)
    _abox_platform_attach_system_peripheral(${target} pinmux ${ARGN})
endfunction()

function(abox_platform_attach_low_power target)
    cmake_parse_arguments(ABOX_LP "" "HARDWARE" "" ${ARGN})
    if(NOT TARGET ${target} OR ABOX_LP_UNPARSED_ARGUMENTS OR ABOX_LP_KEYWORDS_MISSING_VALUES OR
       NOT ABOX_LP_HARDWARE MATCHES "^(stm32f105_ec800_v1|gd32f105_ec800_v1)$")
        message(FATAL_ERROR "abox_platform_attach_low_power: invalid target or hardware")
    endif()
    get_target_property(existing ${target} ABOX_LOW_POWER_HARDWARE)
    if(existing)
        if(NOT existing STREQUAL ABOX_LP_HARDWARE)
            message(FATAL_ERROR "abox_platform_attach_low_power: target has a different MCU")
        endif()
        return()
    endif()
    abox_platform_attach_clock(${target} HARDWARE "${ABOX_LP_HARDWARE}")
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_low_power.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/low_power_f105.c")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/internal/include")
    set_property(TARGET ${target} PROPERTY ABOX_LOW_POWER_HARDWARE "${ABOX_LP_HARDWARE}")
endfunction()

function(abox_platform_attach_smbus target)
    cmake_parse_arguments(ABOX_SMBUS "" "HARDWARE" "" ${ARGN})
    if(NOT TARGET ${target} OR ABOX_SMBUS_UNPARSED_ARGUMENTS OR ABOX_SMBUS_KEYWORDS_MISSING_VALUES OR
       NOT ABOX_SMBUS_HARDWARE MATCHES "^(stm32f105_ec800_v1|gd32f105_ec800_v1)$")
        message(FATAL_ERROR "abox_platform_attach_smbus: invalid target or hardware")
    endif()
    get_target_property(existing ${target} ABOX_SMBUS_HARDWARE)
    if(existing)
        if(NOT existing STREQUAL ABOX_SMBUS_HARDWARE)
            message(FATAL_ERROR "abox_platform_attach_smbus: target has a different MCU")
        endif()
        return()
    endif()
    abox_platform_attach_i2c(${target} HARDWARE "${ABOX_SMBUS_HARDWARE}")
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_smbus.c")
    set_property(TARGET ${target} PROPERTY ABOX_SMBUS_HARDWARE "${ABOX_SMBUS_HARDWARE}")
endfunction()

function(abox_platform_attach_usb_otg target)
    cmake_parse_arguments(ABOX_OTG "" "HARDWARE" "" ${ARGN})
    if(NOT TARGET ${target} OR ABOX_OTG_UNPARSED_ARGUMENTS OR ABOX_OTG_KEYWORDS_MISSING_VALUES OR
       NOT ABOX_OTG_HARDWARE MATCHES "^(stm32f105_ec800_v1|gd32f105_ec800_v1)$")
        message(FATAL_ERROR "abox_platform_attach_usb_otg: invalid target or hardware")
    endif()
    get_target_property(existing ${target} ABOX_USB_OTG_HARDWARE)
    if(existing)
        if(NOT existing STREQUAL ABOX_OTG_HARDWARE)
            message(FATAL_ERROR "abox_platform_attach_usb_otg: target has a different MCU")
        endif()
        return()
    endif()
    abox_platform_attach_usb(${target} HARDWARE "${ABOX_OTG_HARDWARE}")
    target_compile_definitions(${target} PRIVATE ABOX_USB_OTG_MANAGED=1)
    if(ABOX_OTG_HARDWARE STREQUAL "gd32f105_ec800_v1")
        target_compile_definitions(${target} PRIVATE ABOX_MCU_GD32F105=1)
    endif()
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_usb_otg.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/usb_otg_f105.c")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/internal/include")
    set_property(TARGET ${target} PROPERTY ABOX_USB_OTG_HARDWARE "${ABOX_OTG_HARDWARE}")
endfunction()

# RTC is opt-in; Open never resets the backup domain implicitly.
function(abox_platform_attach_rtc target)
    cmake_parse_arguments(ABOX_RTC "" "HARDWARE" "" ${ARGN})
    if(NOT TARGET ${target} OR ABOX_RTC_UNPARSED_ARGUMENTS OR ABOX_RTC_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "abox_platform_attach_rtc: invalid arguments")
    endif()
    if(ABOX_RTC_HARDWARE STREQUAL "stm32f105_ec800_v1")
        set(mcu stm32f105)
    elseif(ABOX_RTC_HARDWARE STREQUAL "gd32f105_ec800_v1")
        set(mcu gd32f105)
    else()
        message(FATAL_ERROR "abox_platform_attach_rtc: unsupported HARDWARE")
    endif()
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_rtc.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/${mcu}/rtc_${mcu}.c")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/internal/include")
    target_link_libraries(${target} abox::core)
endfunction()

# Optional dual DAC channels; the board owns pins and trigger sources.
function(abox_platform_attach_dac target)
    cmake_parse_arguments(ABOX_DAC "" "HARDWARE" "" ${ARGN})
    if(NOT TARGET ${target} OR ABOX_DAC_UNPARSED_ARGUMENTS OR ABOX_DAC_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "abox_platform_attach_dac: invalid arguments")
    endif()
    if(ABOX_DAC_HARDWARE STREQUAL "stm32f105_ec800_v1")
        set(mcu stm32f105)
        target_compile_definitions(${target} PRIVATE HAL_DAC_MODULE_ENABLED)
        target_sources(${target} PRIVATE
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../vendor/stm32f1/hal-1.1.10/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_dac.c"
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../vendor/stm32f1/hal-1.1.10/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_dac_ex.c")
    elseif(ABOX_DAC_HARDWARE STREQUAL "gd32f105_ec800_v1")
        set(mcu gd32f105)
        target_sources(${target} PRIVATE
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../vendor/gd32f10x/2.7.0/GD32F10x_standard_peripheral/Source/gd32f10x_dac.c")
    else()
        message(FATAL_ERROR "abox_platform_attach_dac: unsupported HARDWARE")
    endif()
    abox_platform_attach_dma(${target} HARDWARE ${ABOX_DAC_HARDWARE})
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_dac.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/${mcu}/dac_${mcu}.c")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/internal/include")
endfunction()

# GPIO EXTI lines are shared by pin number; board code supplies input and NVIC.
function(abox_platform_attach_exti target)
    cmake_parse_arguments(ABOX_EXTI "" "HARDWARE" "" ${ARGN})
    if(NOT TARGET ${target} OR ABOX_EXTI_UNPARSED_ARGUMENTS OR ABOX_EXTI_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "abox_platform_attach_exti: invalid arguments")
    endif()
    if(ABOX_EXTI_HARDWARE STREQUAL "stm32f105_ec800_v1")
        set(mcu stm32f105)
    elseif(ABOX_EXTI_HARDWARE STREQUAL "gd32f105_ec800_v1")
        set(mcu gd32f105)
    else()
        message(FATAL_ERROR "abox_platform_attach_exti: unsupported HARDWARE")
    endif()
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_exti.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/${mcu}/exti_${mcu}.c")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/internal/include")
endfunction()

# Optional USB OTG FS controller API. GPIO, clock tree, NVIC and the external
# VBUS switch belong to the board and are not initialized by this attachment.
function(abox_platform_attach_usb target)
    cmake_parse_arguments(ABOX_USB "" "HARDWARE" "" ${ARGN})
    if(NOT TARGET ${target} OR ABOX_USB_UNPARSED_ARGUMENTS OR ABOX_USB_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "abox_platform_attach_usb: invalid arguments")
    endif()
    get_target_property(existing ${target} ABOX_USB_HARDWARE)
    if(existing)
        if(NOT existing STREQUAL ABOX_USB_HARDWARE)
            message(FATAL_ERROR "abox_platform_attach_usb: target has a different MCU")
        endif()
        return()
    endif()
    if(ABOX_USB_HARDWARE STREQUAL "stm32f105_ec800_v1")
        set(mcu stm32f105)
        set(vendor "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../vendor/stm32f1/hal-1.1.10/STM32F1xx_HAL_Driver")
        target_compile_definitions(${target} PRIVATE HAL_PCD_MODULE_ENABLED HAL_HCD_MODULE_ENABLED)
        target_sources(${target} PRIVATE "${vendor}/Src/stm32f1xx_hal_pcd.c" "${vendor}/Src/stm32f1xx_hal_pcd_ex.c"
            "${vendor}/Src/stm32f1xx_hal_hcd.c" "${vendor}/Src/stm32f1xx_ll_usb.c")
    elseif(ABOX_USB_HARDWARE STREQUAL "gd32f105_ec800_v1")
        set(mcu gd32f105)
        set(vendor "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../vendor/gd32f10x/2.7.0/GD32F10x_usbfs_library")
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/gd32f105/usb_config" "${vendor}/Include")
        target_sources(${target} PRIVATE "${vendor}/Source/drv_usb_core.c" "${vendor}/Source/drv_usb_dev.c"
            "${vendor}/Source/drv_usb_host.c" "${vendor}/Source/drv_usbd_int.c" "${vendor}/Source/drv_usbh_int.c")
    else()
        message(FATAL_ERROR "abox_platform_attach_usb: unsupported HARDWARE")
    endif()
    abox_platform_attach_critical(${target} HARDWARE "${ABOX_USB_HARDWARE}")
    target_compile_definitions(${target} PRIVATE ABOX_USB_API_ENABLED=1)
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_usb.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/${mcu}/usb_${mcu}.c")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/internal/include")
    target_link_libraries(${target} abox::core)
    set_property(TARGET ${target} PROPERTY ABOX_USB_HARDWARE "${ABOX_USB_HARDWARE}")
endfunction()

function(abox_platform_attach_spi target)
    cmake_parse_arguments(ABOX_SPI "" "HARDWARE" "" ${ARGN})
    if(NOT TARGET ${target} OR ABOX_SPI_UNPARSED_ARGUMENTS OR ABOX_SPI_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "abox_platform_attach_spi: invalid arguments")
    endif()
    get_target_property(existing ${target} ABOX_SPI_HARDWARE)
    if(existing)
        if(NOT existing STREQUAL ABOX_SPI_HARDWARE)
            message(FATAL_ERROR "abox_platform_attach_spi: target has a different MCU")
        endif()
        return()
    endif()
    if(ABOX_SPI_HARDWARE STREQUAL "stm32f105_ec800_v1")
        set(mcu stm32f105)
        target_compile_definitions(${target} PRIVATE HAL_SPI_MODULE_ENABLED)
        target_sources(${target} PRIVATE
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../vendor/stm32f1/hal-1.1.10/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_spi.c")
    elseif(ABOX_SPI_HARDWARE STREQUAL "gd32f105_ec800_v1")
        set(mcu gd32f105)
        target_sources(${target} PRIVATE
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../vendor/gd32f10x/2.7.0/GD32F10x_standard_peripheral/Source/gd32f10x_spi.c")
    else()
        message(FATAL_ERROR "abox_platform_attach_spi: unsupported HARDWARE")
    endif()
    abox_platform_attach_dma(${target} HARDWARE ${ABOX_SPI_HARDWARE})
    set_property(TARGET ${target} PROPERTY ABOX_SPI_HARDWARE "${ABOX_SPI_HARDWARE}")
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/${mcu}/spi_${mcu}.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_spi_dma.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_spi_crc.c")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/internal/include")
    target_link_libraries(${target} abox::core)
endfunction()

function(abox_platform_attach_i2s target)
    cmake_parse_arguments(ABOX_I2S "" "HARDWARE" "" ${ARGN})
    if(NOT TARGET ${target} OR ABOX_I2S_UNPARSED_ARGUMENTS OR ABOX_I2S_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "abox_platform_attach_i2s: invalid arguments")
    endif()
    get_target_property(existing ${target} ABOX_I2S_HARDWARE)
    if(existing)
        if(NOT existing STREQUAL ABOX_I2S_HARDWARE)
            message(FATAL_ERROR "abox_platform_attach_i2s: target has a different MCU")
        endif()
        return()
    endif()
    abox_platform_attach_spi(${target} HARDWARE ${ABOX_I2S_HARDWARE})
    set_property(TARGET ${target} PROPERTY ABOX_I2S_HARDWARE "${ABOX_I2S_HARDWARE}")
    target_compile_definitions(${target} PRIVATE ABOX_I2S_API_ENABLED=1)
    if(ABOX_I2S_HARDWARE STREQUAL "gd32f105_ec800_v1")
        target_compile_definitions(${target} PRIVATE ABOX_MCU_GD32F105=1)
    endif()
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_i2s.c")
endfunction()

# Optional window watchdog. Enabling the interface does not start hardware.
function(abox_platform_attach_wwdg target)
    cmake_parse_arguments(ABOX_WWDG "" "HARDWARE" "" ${ARGN})
    if(NOT TARGET ${target} OR ABOX_WWDG_UNPARSED_ARGUMENTS OR ABOX_WWDG_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "abox_platform_attach_wwdg: invalid arguments")
    endif()
    get_target_property(existing ${target} ABOX_WWDG_HARDWARE)
    if(existing)
        if(NOT existing STREQUAL ABOX_WWDG_HARDWARE)
            message(FATAL_ERROR "abox_platform_attach_wwdg: target has a different MCU")
        endif()
        return()
    endif()
    if(ABOX_WWDG_HARDWARE STREQUAL "stm32f105_ec800_v1")
        set(mcu stm32f105)
    elseif(ABOX_WWDG_HARDWARE STREQUAL "gd32f105_ec800_v1")
        set(mcu gd32f105)
    else()
        message(FATAL_ERROR "abox_platform_attach_wwdg: unsupported HARDWARE")
    endif()
    abox_platform_attach_critical(${target} HARDWARE ${ABOX_WWDG_HARDWARE})
    set_property(TARGET ${target} PROPERTY ABOX_WWDG_HARDWARE "${ABOX_WWDG_HARDWARE}")
    target_compile_definitions(${target} PRIVATE ABOX_WWDG_API_ENABLED=1)
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_wwdg.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/${mcu}/wwdg_${mcu}.c")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/internal/include")
endfunction()

# Shared claim table for ABox DMA and UART, linked once per application.
function(abox_platform_attach_dma_resource target hardware)
    get_target_property(existing ${target} ABOX_DMA_RESOURCE_HARDWARE)
    if(existing)
        if(NOT existing STREQUAL hardware)
            message(FATAL_ERROR "DMA resource MCU mismatch")
        endif()
        return()
    endif()
    abox_platform_attach_critical(${target} HARDWARE ${hardware})
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_dma_resource.c")
    set_property(TARGET ${target} PROPERTY ABOX_DMA_RESOURCE_HARDWARE "${hardware}")
endfunction()

# The standard STM32 vendor set and GD32 runtime already include the vendor
# DMA driver. The application assigns channels and dispatches their IRQs.
function(abox_platform_attach_dma target)
    cmake_parse_arguments(ABOX_DMA "" "HARDWARE" "" ${ARGN})
    if(NOT TARGET ${target} OR ABOX_DMA_UNPARSED_ARGUMENTS OR ABOX_DMA_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "abox_platform_attach_dma: invalid arguments")
    endif()
    get_target_property(existing ${target} ABOX_DMA_HARDWARE)
    if(existing)
        if(NOT existing STREQUAL ABOX_DMA_HARDWARE)
            message(FATAL_ERROR "abox_platform_attach_dma: target has a different MCU")
        endif()
        return()
    endif()
    if(ABOX_DMA_HARDWARE STREQUAL "stm32f105_ec800_v1")
        set(mcu stm32f105)
    elseif(ABOX_DMA_HARDWARE STREQUAL "gd32f105_ec800_v1")
        set(mcu gd32f105)
    else()
        message(FATAL_ERROR "abox_platform_attach_dma: unsupported HARDWARE")
    endif()
    abox_platform_attach_dma_resource(${target} "${ABOX_DMA_HARDWARE}")
    target_compile_definitions(${target} PRIVATE ABOX_DMA_API_ENABLED=1)
    set_property(TARGET ${target} PROPERTY ABOX_DMA_HARDWARE "${ABOX_DMA_HARDWARE}")
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/${mcu}/dma_${mcu}.c")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/internal/include")
endfunction()

# Optional physical timer/PWM API; the caller reserves timers and pins.
function(abox_platform_attach_timer target)
    cmake_parse_arguments(ABOX_TIMER "" "HARDWARE" "" ${ARGN})
    if(NOT TARGET ${target} OR ABOX_TIMER_UNPARSED_ARGUMENTS OR ABOX_TIMER_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "abox_platform_attach_timer: invalid arguments")
    endif()
    if(ABOX_TIMER_HARDWARE STREQUAL "stm32f105_ec800_v1")
        set(mcu stm32f105)
    elseif(ABOX_TIMER_HARDWARE STREQUAL "gd32f105_ec800_v1")
        set(mcu gd32f105)
    else()
        message(FATAL_ERROR "abox_platform_attach_timer: unsupported HARDWARE")
    endif()
    abox_platform_attach_dma(${target} HARDWARE ${ABOX_TIMER_HARDWARE})
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/${mcu}/timer_${mcu}.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_capture_dma.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_pwm_dma.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_timer_burst.c")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/internal/include")
endfunction()

# Optional ADC API. Both ADCs and their shared clock are owned by its user.
function(abox_platform_attach_adc target)
    cmake_parse_arguments(ABOX_ADC "" "HARDWARE" "" ${ARGN})
    if(NOT TARGET ${target} OR ABOX_ADC_UNPARSED_ARGUMENTS OR ABOX_ADC_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "abox_platform_attach_adc: invalid arguments")
    endif()
    if(ABOX_ADC_HARDWARE STREQUAL "stm32f105_ec800_v1")
        set(mcu stm32f105)
        target_compile_definitions(${target} PRIVATE HAL_ADC_MODULE_ENABLED)
        target_sources(${target} PRIVATE
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../vendor/stm32f1/hal-1.1.10/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_adc.c"
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../vendor/stm32f1/hal-1.1.10/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_adc_ex.c")
    elseif(ABOX_ADC_HARDWARE STREQUAL "gd32f105_ec800_v1")
        set(mcu gd32f105)
        target_sources(${target} PRIVATE
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../vendor/gd32f10x/2.7.0/GD32F10x_standard_peripheral/Source/gd32f10x_adc.c")
    else()
        message(FATAL_ERROR "abox_platform_attach_adc: unsupported HARDWARE")
    endif()
    abox_platform_attach_dma(${target} HARDWARE ${ABOX_ADC_HARDWARE})
    target_compile_definitions(${target} PRIVATE ABOX_ADC_API_ENABLED=1)
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/${mcu}/adc_${mcu}.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_adc_dma.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_adc_dual_injected.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_adc_watch.c")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/internal/include")
    target_link_libraries(${target} abox::core)
endfunction()

# Optional I2C master/slave API. GPIO remapping and bus recovery are board-owned.
function(abox_platform_attach_i2c target)
    cmake_parse_arguments(ABOX_I2C "" "HARDWARE" "" ${ARGN})
    if(NOT TARGET ${target} OR ABOX_I2C_UNPARSED_ARGUMENTS OR ABOX_I2C_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "abox_platform_attach_i2c: invalid arguments")
    endif()
    get_target_property(existing ${target} ABOX_I2C_HARDWARE)
    if(existing)
        if(NOT existing STREQUAL ABOX_I2C_HARDWARE)
            message(FATAL_ERROR "abox_platform_attach_i2c: target has a different MCU")
        endif()
        return()
    endif()
    target_compile_definitions(${target} PRIVATE ABOX_I2C_API_ENABLED=1)
    if(ABOX_I2C_HARDWARE STREQUAL "stm32f105_ec800_v1")
        set(mcu stm32f105)
        target_compile_definitions(${target} PRIVATE HAL_I2C_MODULE_ENABLED)
        target_sources(${target} PRIVATE
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../vendor/stm32f1/hal-1.1.10/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_i2c.c")
    elseif(ABOX_I2C_HARDWARE STREQUAL "gd32f105_ec800_v1")
        set(mcu gd32f105)
        target_sources(${target} PRIVATE
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../vendor/gd32f10x/2.7.0/GD32F10x_standard_peripheral/Source/gd32f10x_i2c.c")
    else()
        message(FATAL_ERROR "abox_platform_attach_i2c: unsupported HARDWARE")
    endif()
    abox_platform_attach_dma(${target} HARDWARE ${ABOX_I2C_HARDWARE})
    target_sources(${target} PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_i2c.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/${mcu}/i2c_${mcu}.c")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/internal/include")
    target_link_libraries(${target} abox::core)
    set_property(TARGET ${target} PROPERTY ABOX_I2C_HARDWARE "${ABOX_I2C_HARDWARE}")
endfunction()

function(abox_platform_attach_flash target)
    cmake_parse_arguments(ABOX_FLASH "" "HARDWARE" "" ${ARGN})
    if(NOT TARGET ${target} OR ABOX_FLASH_UNPARSED_ARGUMENTS OR ABOX_FLASH_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "abox_platform_attach_flash: invalid arguments")
    endif()
    get_target_property(existing ${target} ABOX_FLASH_HARDWARE)
    if(existing)
        if(NOT existing STREQUAL ABOX_FLASH_HARDWARE)
            message(FATAL_ERROR "abox_platform_attach_flash: target has a different Flash backend")
        endif()
        return()
    endif()
    if(ABOX_FLASH_HARDWARE STREQUAL "stm32f105_ec800_v1")
        set(backend "hardware/mcu/stm32f105/flash_stm32f105.c")
    elseif(ABOX_FLASH_HARDWARE STREQUAL "gd32f105_ec800_v1")
        set(backend "hardware/mcu/gd32f105/flash_api_gd32f105.c")
    else()
        message(FATAL_ERROR "abox_platform_attach_flash: unsupported HARDWARE")
    endif()
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_flash.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../${backend}")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/internal/include")
    set_property(TARGET ${target} PROPERTY ABOX_FLASH_HARDWARE "${ABOX_FLASH_HARDWARE}")
endfunction()

function(abox_platform_attach_flash_options target)
    cmake_parse_arguments(ABOX_OPTIONS "" "HARDWARE" "" ${ARGN})
    if(NOT TARGET ${target} OR ABOX_OPTIONS_UNPARSED_ARGUMENTS OR ABOX_OPTIONS_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "abox_platform_attach_flash_options: invalid arguments")
    endif()
    abox_platform_attach_flash(${target} HARDWARE "${ABOX_OPTIONS_HARDWARE}")
    get_target_property(existing ${target} ABOX_FLASH_OPTIONS_HARDWARE)
    if(existing)
        if(NOT existing STREQUAL ABOX_OPTIONS_HARDWARE)
            message(FATAL_ERROR "abox_platform_attach_flash_options: different backend already attached")
        endif()
        return()
    endif()
    if(ABOX_OPTIONS_HARDWARE STREQUAL "stm32f105_ec800_v1")
        set(mcu stm32f105)
    elseif(ABOX_OPTIONS_HARDWARE STREQUAL "gd32f105_ec800_v1")
        set(mcu gd32f105)
    else()
        message(FATAL_ERROR "abox_platform_attach_flash_options: unsupported HARDWARE")
    endif()
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_flash_options.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/${mcu}/flash_options_${mcu}.c")
    set_property(TARGET ${target} PROPERTY ABOX_FLASH_OPTIONS_HARDWARE "${ABOX_OPTIONS_HARDWARE}")
endfunction()

function(abox_platform_attach_time target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "abox_platform_attach_time: invalid target")
    endif()
    get_target_property(existing ${target} ABOX_TIME_ATTACHED)
    if(existing)
        return()
    endif()
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_time.c")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include")
    target_link_libraries(${target} abox::core)
    set_property(TARGET ${target} PROPERTY ABOX_TIME_ATTACHED TRUE)
endfunction()

function(abox_platform_attach_critical target)
    cmake_parse_arguments(ABOX_CRITICAL "" "HARDWARE" "" ${ARGN})
    if(NOT TARGET ${target} OR ABOX_CRITICAL_UNPARSED_ARGUMENTS OR ABOX_CRITICAL_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "abox_platform_attach_critical: invalid arguments")
    endif()
    if(NOT ABOX_CRITICAL_HARDWARE STREQUAL "stm32f105_ec800_v1" AND
       NOT ABOX_CRITICAL_HARDWARE STREQUAL "gd32f105_ec800_v1")
        message(FATAL_ERROR "abox_platform_attach_critical: unsupported HARDWARE")
    endif()
    get_target_property(existing ${target} ABOX_CRITICAL_HARDWARE)
    if(existing)
        if(NOT existing STREQUAL ABOX_CRITICAL_HARDWARE)
            message(FATAL_ERROR "abox_platform_attach_critical: target has a different MCU")
        endif()
        return()
    endif()
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/critical_cortexm3.c")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include")
    set_property(TARGET ${target} PROPERTY ABOX_CRITICAL_HARDWARE "${ABOX_CRITICAL_HARDWARE}")
endfunction()

function(abox_platform_attach_clock target)
    cmake_parse_arguments(ABOX_CLOCK "" "HARDWARE" "" ${ARGN})
    if(NOT TARGET ${target} OR ABOX_CLOCK_UNPARSED_ARGUMENTS OR ABOX_CLOCK_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "abox_platform_attach_clock: invalid arguments")
    endif()
    get_target_property(existing ${target} ABOX_CLOCK_HARDWARE)
    if(existing)
        if(NOT existing STREQUAL ABOX_CLOCK_HARDWARE)
            message(FATAL_ERROR "abox_platform_attach_clock: target has a different MCU")
        endif()
        return()
    endif()
    if(ABOX_CLOCK_HARDWARE STREQUAL "stm32f105_ec800_v1")
        set(mcu stm32f105)
    elseif(ABOX_CLOCK_HARDWARE STREQUAL "gd32f105_ec800_v1")
        set(mcu gd32f105)
        target_compile_definitions(${target} PRIVATE ABOX_MCU_GD32F105=1)
    else()
        message(FATAL_ERROR "abox_platform_attach_clock: unsupported HARDWARE")
    endif()
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_mcu_clock.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/${mcu}/clock_${mcu}.c")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/internal/include")
    set_property(TARGET ${target} PROPERTY ABOX_CLOCK_HARDWARE "${ABOX_CLOCK_HARDWARE}")
endfunction()

function(abox_platform_attach_serial_transaction target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "abox_platform_attach_serial_transaction: target does not exist")
    endif()
    target_sources(${target} PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_serial_transaction.c")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include")
endfunction()

function(abox_platform_attach_target_identity target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "abox_platform_attach_target_identity: target '${target}' does not exist")
    endif()
    cmake_parse_arguments(ABOX_ID "" "HARDWARE" "" ${ARGN})
    if(ABOX_ID_UNPARSED_ARGUMENTS OR ABOX_ID_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "abox_platform_attach_target_identity: invalid arguments")
    endif()
    if(ABOX_ID_HARDWARE STREQUAL "stm32f105_ec800_v1")
        set(mcu stm32f105)
    elseif(ABOX_ID_HARDWARE STREQUAL "gd32f105_ec800_v1")
        set(mcu gd32f105)
    else()
        message(FATAL_ERROR "abox_platform_attach_target_identity: unsupported HARDWARE")
    endif()
    target_sources(${target} PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/src/abox_target_identity.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/${mcu}/mcu_${mcu}.c")
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include")
    target_link_libraries(${target} abox::enrollment abox::boot_v2_common)
endfunction()

# Unified UART API and board-specific resource mapping.
function(abox_platform_attach_uart target)
    cmake_parse_arguments(ABOX_UART "" "HARDWARE" "" ${ARGN})
    if(NOT TARGET ${target} OR ABOX_UART_UNPARSED_ARGUMENTS OR ABOX_UART_KEYWORDS_MISSING_VALUES OR NOT ABOX_UART_HARDWARE)
        message(FATAL_ERROR "abox_platform_attach_uart: HARDWARE is required")
    endif()
    if(ABOX_UART_HARDWARE STREQUAL "stm32f105_ec800_v1")
        set(mcu stm32f105)
    elseif(ABOX_UART_HARDWARE STREQUAL "gd32f105_ec800_v1")
        set(mcu gd32f105)
    else()
        message(FATAL_ERROR "abox_platform_attach_uart: unsupported HARDWARE")
    endif()
    abox_platform_attach_dma_resource(${target} "${ABOX_UART_HARDWARE}")
    target_sources(${target} PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/uart_f105.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/${mcu}/uart_hw_${mcu}.c"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/boards/board_uart_${mcu}.c")
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/api/include"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/internal/include")
    target_link_libraries(${target} abox::core)
endfunction()

# Opt-in standard App HAL/Flash/EC800 power port. Products with a different
# board or Flash layout continue to supply their own ABoxPlatformPort.
function(abox_platform_attach_standard_port target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "abox_platform_attach_standard_port: target '${target}' does not exist")
    endif()
    cmake_parse_arguments(ABOX_PORT "" "HARDWARE" "" ${ARGN})
    if(ABOX_PORT_UNPARSED_ARGUMENTS OR ABOX_PORT_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "abox_platform_attach_standard_port: invalid arguments")
    endif()
    abox_platform_attach_flash(${target} HARDWARE ${ABOX_PORT_HARDWARE})
    abox_platform_attach_time(${target})
    abox_platform_attach_critical(${target} HARDWARE ${ABOX_PORT_HARDWARE})
    abox_platform_attach_clock(${target} HARDWARE ${ABOX_PORT_HARDWARE})
    if(ABOX_PORT_HARDWARE STREQUAL "stm32f105_ec800_v1")
        target_sources(${target} PRIVATE
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../targets/stm32f105_ec800_v1/platform_port_stm32f105.c")
        target_include_directories(${target} PRIVATE
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../targets/stm32f105_ec800_v1/include")
    elseif(ABOX_PORT_HARDWARE STREQUAL "gd32f105_ec800_v1")
        target_sources(${target} PRIVATE
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/boards/abox_v1/board_gd32f105.c")
    else()
        message(FATAL_ERROR "abox_platform_attach_standard_port: unsupported HARDWARE")
    endif()
endfunction()

# Standard board link set for a new App. This does not create the product's
# startup, control/safety task or protocol handlers; it keeps one explicit
# scheduler and hardware contract while reusing the common communication stack.
function(abox_platform_attach_standard_clock target)
    target_sources(${target} PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../targets/stm32f105_ec800_v1/monotonic_stm32f105.c")
    target_compile_definitions(${target} PRIVATE ABOX_STM32_MONOTONIC=1)
endfunction()

function(abox_platform_attach_gd32f105_runtime target)
    if(NOT TARGET ${target} OR NOT CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR "abox_platform_attach_gd32f105_runtime: cross-compiled target required")
    endif()
    set(gd "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/gd32f105")
    set(board "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/boards/abox_v1")
    set(vendor "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../vendor/gd32f10x/2.7.0")
    target_include_directories(${target} BEFORE PRIVATE "${gd}/include" "${board}/include"
        "${vendor}/CMSIS" "${vendor}/CMSIS/GD/GD32F10x/Include"
        "${vendor}/GD32F10x_standard_peripheral/Include")
    target_compile_definitions(${target} PRIVATE ABOX_MCU_GD32F105=1 GD32F10X_CL HXTAL_VALUE=8000000U)
    target_sources(${target} PRIVATE "${gd}/startup_gd32f105.c" "${gd}/flash_gd32f105.c"
        "${gd}/monotonic_gd32f105.c"
        "${vendor}/GD32F10x_standard_peripheral/Source/gd32f10x_fmc.c"
        "${vendor}/GD32F10x_standard_peripheral/Source/gd32f10x_gpio.c"
        "${vendor}/GD32F10x_standard_peripheral/Source/gd32f10x_rcu.c"
        "${vendor}/GD32F10x_standard_peripheral/Source/gd32f10x_usart.c"
        "${vendor}/GD32F10x_standard_peripheral/Source/gd32f10x_dma.c"
        "${vendor}/GD32F10x_standard_peripheral/Source/gd32f10x_timer.c")
    option(ABOX_GD_FLASH_READONLY_TEST "GD bench App: disable all flash mutations" OFF)
    if(ABOX_GD_FLASH_READONLY_TEST)
        target_compile_definitions(${target} PRIVATE ABOX_GD_FLASH_READONLY_TEST=1)
        set_target_properties(${target} PROPERTIES OUTPUT_NAME ABox_GD_App_Readonly_Bench)
        target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tests/gd32_boot_ram_guard.c")
        target_link_options(${target} PRIVATE -Wl,--wrap=ABoxGd32Flash_Begin
            -Wl,--wrap=ABoxGd32Flash_Write -Wl,--wrap=ABoxGd32Flash_ErasePage)
    endif()
    target_link_libraries(${target} m)
endfunction()

function(abox_platform_attach_standard_app target)
    cmake_parse_arguments(ABOX_APP "" "PRODUCT_CONFIG_DIR;SCHEDULER;HARDWARE" "" ${ARGN})
    if(ABOX_APP_UNPARSED_ARGUMENTS OR ABOX_APP_KEYWORDS_MISSING_VALUES OR
            NOT ABOX_APP_PRODUCT_CONFIG_DIR OR NOT ABOX_APP_SCHEDULER)
        message(FATAL_ERROR "abox_platform_attach_standard_app: PRODUCT_CONFIG_DIR and SCHEDULER are required")
    endif()
    if(NOT ABOX_APP_HARDWARE)
        set(ABOX_APP_HARDWARE stm32f105_ec800_v1)
    endif()
    set(ABOX_PRODUCT_CONFIG_DIR "${ABOX_APP_PRODUCT_CONFIG_DIR}")
    abox_platform_attach_components(${target}
        SCHEDULER ${ABOX_APP_SCHEDULER}
        HARDWARE ${ABOX_APP_HARDWARE}
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
    abox_platform_attach_standard_port(${target} HARDWARE ${ABOX_APP_HARDWARE})
    if(ABOX_APP_HARDWARE STREQUAL "stm32f105_ec800_v1")
        target_sources(${target} PRIVATE
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../targets/stm32f105_ec800_v1/enrollment_identity_stm32f105.c")
    endif()
endfunction()

# Attach the product-selected FreeRTOS source and CMSIS-RTOS v2 wrapper to a
# GD target. The caller provides FreeRTOSConfig.h and the pinned kernel source;
# do not mix the GD vendor's older core headers with the CMSIS v5 wrapper.
function(abox_platform_attach_gd32_freertos target source_dir config_dir)
    if(NOT EXISTS "${source_dir}/tasks.c" OR NOT EXISTS "${source_dir}/CMSIS_RTOS_V2/cmsis_os2.c" OR
       NOT EXISTS "${config_dir}/FreeRTOSConfig.h")
        message(FATAL_ERROR "abox_platform_attach_gd32_freertos: kernel source or FreeRTOSConfig.h missing")
    endif()
    set(vendor "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../vendor/gd32f10x/2.7.0")
    set(cmsis "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../vendor/stm32f1/hal-1.1.10/CMSIS/Include")
    set(gd "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/mcu/gd32f105")
    set(kernel_target "${target}_gd32_freertos")
    add_library(${kernel_target} OBJECT
        "${source_dir}/croutine.c" "${source_dir}/event_groups.c" "${source_dir}/list.c"
        "${source_dir}/queue.c" "${source_dir}/stream_buffer.c" "${source_dir}/tasks.c" "${source_dir}/timers.c"
        "${source_dir}/CMSIS_RTOS_V2/cmsis_os2.c" "${source_dir}/portable/MemMang/heap_4.c"
        "${source_dir}/portable/GCC/ARM_CM3/port.c")
    target_include_directories(${kernel_target} PUBLIC "${config_dir}" "${source_dir}/include"
        "${source_dir}/CMSIS_RTOS_V2" "${source_dir}/portable/GCC/ARM_CM3" "${cmsis}"
        "${vendor}/CMSIS/GD/GD32F10x/Include" "${vendor}/CMSIS" "${gd}/include"
        "${vendor}/GD32F10x_standard_peripheral/Include")
    target_compile_definitions(${kernel_target} PUBLIC GD32F10X_CL CMSIS_device_header="gd32f10x.h")
    target_link_libraries(${target} ${kernel_target})
endfunction()

# Single entry for products using the standard F105/EC800 board contract.
# The consumer supplies App sources, boot_cfg.h and the linker script. An
# optional HAL_CONFIG_DIR overrides the standard ST config. CAN is opt-in;
# historical STM32 CAN startup remains unchanged.
function(abox_platform_attach_f105_app target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "abox_platform_attach_f105_app: target does not exist")
    endif()
    cmake_parse_arguments(ABOX_F105 "CAN;CAN_EVENTS" "HARDWARE;PRODUCT_CONFIG_DIR;HAL_CONFIG_DIR;SCHEDULER;FREERTOS_SOURCE_DIR;FREERTOS_CONFIG_DIR" "" ${ARGN})
    if(ABOX_F105_UNPARSED_ARGUMENTS OR ABOX_F105_KEYWORDS_MISSING_VALUES OR
            NOT ABOX_F105_HARDWARE OR NOT ABOX_F105_PRODUCT_CONFIG_DIR)
        message(FATAL_ERROR "abox_platform_attach_f105_app: HARDWARE and PRODUCT_CONFIG_DIR are required")
    endif()
    if(NOT ABOX_F105_SCHEDULER)
        set(ABOX_F105_SCHEDULER BAREMETAL)
    endif()
    if(NOT ABOX_F105_SCHEDULER STREQUAL "BAREMETAL" AND
       NOT (ABOX_F105_SCHEDULER STREQUAL "FREERTOS" AND ABOX_F105_HARDWARE STREQUAL "gd32f105_ec800_v1"))
        message(FATAL_ERROR "abox_platform_attach_f105_app: unsupported scheduler and hardware combination")
    endif()
    get_target_property(already_attached ${target} ABOX_F105_APP_ATTACHED)
    if(already_attached)
        message(FATAL_ERROR "abox_platform_attach_f105_app: target already attached")
    endif()
    if(ABOX_F105_HARDWARE STREQUAL "stm32f105_ec800_v1")
        if(NOT ABOX_F105_HAL_CONFIG_DIR)
            set(ABOX_F105_HAL_CONFIG_DIR
                "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/boards/abox_v2/hal_config")
        endif()
        if(NOT EXISTS "${ABOX_F105_HAL_CONFIG_DIR}/stm32f1xx_hal_conf.h")
            message(FATAL_ERROR "abox_platform_attach_f105_app: HAL_CONFIG_DIR must contain stm32f1xx_hal_conf.h")
        endif()
        target_include_directories(${target} PRIVATE "${ABOX_F105_HAL_CONFIG_DIR}")
        abox_platform_attach_stm32f105_vendor(${target})
        abox_platform_attach_stm32f105_board_sources(${target})
        abox_platform_attach_standard_clock(${target})
        target_link_libraries(${target} m)
    elseif(ABOX_F105_HARDWARE STREQUAL "gd32f105_ec800_v1")
        abox_platform_attach_gd32f105_runtime(${target})
        if(ABOX_F105_SCHEDULER STREQUAL "FREERTOS")
            target_compile_definitions(${target} PRIVATE ABOX_GD_FREERTOS=1)
            abox_platform_attach_gd32_freertos(${target} "${ABOX_F105_FREERTOS_SOURCE_DIR}" "${ABOX_F105_FREERTOS_CONFIG_DIR}")
        endif()
    else()
        message(FATAL_ERROR "abox_platform_attach_f105_app: unsupported HARDWARE '${ABOX_F105_HARDWARE}'")
    endif()
    abox_platform_attach_standard_app(${target}
        PRODUCT_CONFIG_DIR "${ABOX_F105_PRODUCT_CONFIG_DIR}" SCHEDULER ${ABOX_F105_SCHEDULER}
        HARDWARE ${ABOX_F105_HARDWARE})
    abox_platform_attach_board_startup(${target} HARDWARE ${ABOX_F105_HARDWARE})
    abox_platform_attach_gpio(${target} HARDWARE ${ABOX_F105_HARDWARE})
    abox_platform_attach_uart(${target} HARDWARE ${ABOX_F105_HARDWARE})
    abox_platform_attach_target_identity(${target} HARDWARE ${ABOX_F105_HARDWARE})
    if(ABOX_F105_CAN)
        abox_platform_attach_can_port(${target} HARDWARE ${ABOX_F105_HARDWARE})
        if(ABOX_F105_HARDWARE STREQUAL "gd32f105_ec800_v1")
            target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../hardware/boards/abox_v1/board_can_gd32f105.c")
            target_compile_definitions(${target} PRIVATE ABOX_V1_CAN_BOARD=1)
        endif()
    endif()
    if(ABOX_F105_CAN_EVENTS)
        if(NOT ABOX_F105_CAN)
            message(FATAL_ERROR "abox_platform_attach_f105_app: CAN_EVENTS requires CAN")
        endif()
        abox_platform_attach_can_events(${target} HARDWARE ${ABOX_F105_HARDWARE})
    endif()
    set_property(TARGET ${target} PROPERTY ABOX_F105_APP_ATTACHED TRUE)
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
