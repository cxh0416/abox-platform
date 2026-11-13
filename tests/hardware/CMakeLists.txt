cmake_minimum_required(VERSION 3.22)
project(ABoxGdV1UartBench LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(vendor "${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/gd32f10x/2.7.0")
set(mcu "${CMAKE_CURRENT_SOURCE_DIR}/../../hardware/mcu/gd32f105")
add_executable(gd32_v1_uart_loopback gd32_v1_uart_loopback.c
    "${mcu}/serial_port_gd32f105.c" ../../hardware/api/src/abox_serial_events.c
    "${vendor}/GD32F10x_standard_peripheral/Source/gd32f10x_gpio.c"
    "${vendor}/GD32F10x_standard_peripheral/Source/gd32f10x_rcu.c"
    "${vendor}/GD32F10x_standard_peripheral/Source/gd32f10x_usart.c")
target_include_directories(gd32_v1_uart_loopback PRIVATE ../../hardware/api/include
    ../../components/core/include "${mcu}/include" "${vendor}/CMSIS"
    "${vendor}/CMSIS/GD/GD32F10x/Include" "${vendor}/GD32F10x_standard_peripheral/Include")
target_compile_definitions(gd32_v1_uart_loopback PRIVATE GD32F10X_CL HXTAL_VALUE=8000000U)
target_compile_options(gd32_v1_uart_loopback PRIVATE -mcpu=cortex-m3 -mthumb -Os -ffunction-sections -fdata-sections -Wall -Wextra)
target_link_options(gd32_v1_uart_loopback PRIVATE -mcpu=cortex-m3 -mthumb -nostdlib
    "-T${CMAKE_CURRENT_SOURCE_DIR}/gd32_v1_uart_loopback.ld" -Wl,--gc-sections)
set_property(TARGET gd32_v1_uart_loopback APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/gd32_v1_uart_loopback.ld")
add_custom_command(TARGET gd32_v1_uart_loopback POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:gd32_v1_uart_loopback> gd32_v1_uart_loopback.bin)

add_executable(gd32_v1_can_loopback gd32_v1_can_loopback.c
    "${mcu}/can_port_gd32f105.c"
    "${vendor}/GD32F10x_standard_peripheral/Source/gd32f10x_can.c"
    "${vendor}/GD32F10x_standard_peripheral/Source/gd32f10x_dbg.c"
    "${vendor}/GD32F10x_standard_peripheral/Source/gd32f10x_gpio.c"
    "${vendor}/GD32F10x_standard_peripheral/Source/gd32f10x_rcu.c")
target_include_directories(gd32_v1_can_loopback PRIVATE ../../hardware/api/include
    "${mcu}/include" "${vendor}/CMSIS" "${vendor}/CMSIS/GD/GD32F10x/Include"
    "${vendor}/GD32F10x_standard_peripheral/Include")
target_compile_definitions(gd32_v1_can_loopback PRIVATE GD32F10X_CL HXTAL_VALUE=8000000U)
target_compile_options(gd32_v1_can_loopback PRIVATE -mcpu=cortex-m3 -mthumb -Os -ffunction-sections -fdata-sections -Wall -Wextra)
target_link_options(gd32_v1_can_loopback PRIVATE -mcpu=cortex-m3 -mthumb -nostartfiles --specs=nosys.specs
    "-T${CMAKE_CURRENT_SOURCE_DIR}/gd32_v1_uart_loopback.ld" -Wl,--gc-sections)
set_property(TARGET gd32_v1_can_loopback APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/gd32_v1_uart_loopback.ld")
add_custom_command(TARGET gd32_v1_can_loopback POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:gd32_v1_can_loopback> gd32_v1_can_loopback.bin)
