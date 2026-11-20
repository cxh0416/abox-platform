find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME abox_firmware_serialization_check COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_firmware_serialization.py)
add_test(NAME abox_firmware_serialization_guard_test COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/test_firmware_serialization_guard.py)
add_executable(abox_usb_otg_test abox_usb_otg_test.c ../hardware/api/src/abox_usb_otg.c)
target_include_directories(abox_usb_otg_test PRIVATE ../hardware/api/include ../hardware/internal/include)
add_test(NAME abox_usb_otg_test COMMAND abox_usb_otg_test)
add_executable(abox_usb_otg_managed_test abox_usb_otg_managed_test.c ../hardware/api/src/abox_usb.c ../hardware/api/src/abox_usb_otg.c)
target_compile_definitions(abox_usb_otg_managed_test PRIVATE ABOX_USB_OTG_MANAGED=1)
target_include_directories(abox_usb_otg_managed_test PRIVATE ../hardware/api/include ../hardware/internal/include ../components/core/include)
add_test(NAME abox_usb_otg_managed_test COMMAND abox_usb_otg_managed_test)
add_executable(abox_smbus_test abox_smbus_test.c ../hardware/api/src/abox_smbus.c)
target_include_directories(abox_smbus_test PRIVATE ../hardware/api/include ../hardware/internal/include)
add_test(NAME abox_smbus_test COMMAND abox_smbus_test)
add_executable(abox_low_power_test abox_low_power_test.c ../hardware/api/src/abox_low_power.c)
target_include_directories(abox_low_power_test PRIVATE ../hardware/api/include ../hardware/internal/include)
add_test(NAME abox_low_power_test COMMAND abox_low_power_test)
foreach(mcu IN ITEMS stm32f105 gd32f105)
    add_executable(${mcu}_low_power_backend_test abox_low_power_backend_test.c ../hardware/mcu/low_power_f105.c)
    target_include_directories(${mcu}_low_power_backend_test PRIVATE low_power_stub ../hardware/api/include ../hardware/internal/include)
    if(mcu STREQUAL "gd32f105")
        target_compile_definitions(${mcu}_low_power_backend_test PRIVATE ABOX_MCU_GD32F105=1)
    endif()
    add_test(NAME ${mcu}_low_power_backend_test COMMAND ${mcu}_low_power_backend_test)
endforeach()
foreach(mcu IN ITEMS stm32f105 gd32f105)
    add_executable(${mcu}_pinmux_test abox_pinmux_test.c ../hardware/api/src/abox_pinmux.c
        ../hardware/mcu/${mcu}/pinmux_${mcu}.c)
    target_include_directories(${mcu}_pinmux_test PRIVATE pinmux_stub ../hardware/api/include ../hardware/internal/include)
    if(mcu STREQUAL "stm32f105")
        target_compile_definitions(${mcu}_pinmux_test PRIVATE TEST_STM32=1)
    endif()
    add_test(NAME ${mcu}_pinmux_test COMMAND ${mcu}_pinmux_test)
endforeach()
add_executable(abox_crc_test abox_crc_test.c ../hardware/api/src/abox_crc.c)
target_include_directories(abox_crc_test PRIVATE ../hardware/api/include ../hardware/internal/include)
add_test(NAME abox_crc_test COMMAND abox_crc_test)
add_executable(stm32f105_crc_backend_test abox_crc_backend_test.c ../hardware/mcu/stm32f105/crc_stm32f105.c)
target_include_directories(stm32f105_crc_backend_test PRIVATE crc_stub ../hardware/internal/include)
target_compile_definitions(stm32f105_crc_backend_test PRIVATE TEST_STM32=1)
add_test(NAME stm32f105_crc_backend_test COMMAND stm32f105_crc_backend_test)
add_executable(gd32f105_crc_backend_test abox_crc_backend_test.c ../hardware/mcu/gd32f105/crc_gd32f105.c)
target_include_directories(gd32f105_crc_backend_test PRIVATE crc_stub ../hardware/internal/include)
add_test(NAME gd32f105_crc_backend_test COMMAND gd32f105_crc_backend_test)
add_executable(abox_backup_test abox_backup_test.c ../hardware/api/src/abox_backup.c)
target_include_directories(abox_backup_test PRIVATE ../hardware/api/include ../hardware/internal/include ../components/core/include)
add_test(NAME abox_backup_test COMMAND abox_backup_test)
add_executable(abox_pvd_test abox_pvd_test.c ../hardware/api/src/abox_pvd.c)
target_include_directories(abox_pvd_test PRIVATE ../hardware/api/include ../hardware/internal/include)
add_test(NAME abox_pvd_test COMMAND abox_pvd_test)
foreach(mcu IN ITEMS stm32f105 gd32f105)
    add_executable(${mcu}_power_backend_test abox_power_backend_test.c
        ../hardware/mcu/${mcu}/backup_${mcu}.c ../hardware/mcu/${mcu}/pvd_${mcu}.c)
    target_include_directories(${mcu}_power_backend_test PRIVATE power_stub ../hardware/api/include ../hardware/internal/include)
    if(mcu STREQUAL "stm32f105")
        target_compile_definitions(${mcu}_power_backend_test PRIVATE TEST_STM32=1)
    endif()
    add_test(NAME ${mcu}_power_backend_test COMMAND ${mcu}_power_backend_test)
endforeach()
add_executable(stm32f105_rtc_backend_test abox_rtc_backend_test.c ../hardware/mcu/stm32f105/rtc_stm32f105.c)
target_include_directories(stm32f105_rtc_backend_test PRIVATE rtc_stub ../hardware/api/include ../hardware/internal/include)
target_compile_definitions(stm32f105_rtc_backend_test PRIVATE TEST_STM32=1)
add_test(NAME stm32f105_rtc_backend_test COMMAND stm32f105_rtc_backend_test)
add_executable(gd32f105_rtc_backend_test abox_rtc_backend_test.c ../hardware/mcu/gd32f105/rtc_gd32f105.c)
target_include_directories(gd32f105_rtc_backend_test PRIVATE rtc_stub ../hardware/api/include ../hardware/internal/include)
add_test(NAME gd32f105_rtc_backend_test COMMAND gd32f105_rtc_backend_test)
add_executable(abox_rtc_test abox_rtc_test.c ../hardware/api/src/abox_rtc.c)
target_include_directories(abox_rtc_test PRIVATE ../hardware/api/include ../hardware/internal/include ../components/core/include)
add_test(NAME abox_rtc_test COMMAND abox_rtc_test)
add_executable(abox_dac_test abox_dac_test.c ../hardware/api/src/abox_dac.c)
target_include_directories(abox_dac_test PRIVATE ../hardware/api/include ../hardware/internal/include)
add_test(NAME abox_dac_test COMMAND abox_dac_test)
add_executable(stm32f105_dac_backend_test abox_dac_backend_test.c ../hardware/mcu/stm32f105/dac_stm32f105.c)
target_include_directories(stm32f105_dac_backend_test PRIVATE dac_stub ../hardware/api/include ../hardware/internal/include)
target_compile_definitions(stm32f105_dac_backend_test PRIVATE TEST_STM32=1)
add_test(NAME stm32f105_dac_backend_test COMMAND stm32f105_dac_backend_test)
add_executable(gd32f105_dac_backend_test abox_dac_backend_test.c ../hardware/mcu/gd32f105/dac_gd32f105.c)
target_include_directories(gd32f105_dac_backend_test PRIVATE dac_stub ../hardware/api/include ../hardware/internal/include)
add_test(NAME gd32f105_dac_backend_test COMMAND gd32f105_dac_backend_test)
add_executable(abox_exti_test abox_exti_test.c ../hardware/api/src/abox_exti.c)
target_include_directories(abox_exti_test PRIVATE ../hardware/api/include ../hardware/internal/include)
add_test(NAME abox_exti_test COMMAND abox_exti_test)
add_executable(stm32f105_exti_backend_test abox_exti_backend_test.c ../hardware/mcu/stm32f105/exti_stm32f105.c)
target_include_directories(stm32f105_exti_backend_test PRIVATE exti_stub ../hardware/api/include ../hardware/internal/include)
target_compile_definitions(stm32f105_exti_backend_test PRIVATE TEST_STM32=1)
add_test(NAME stm32f105_exti_backend_test COMMAND stm32f105_exti_backend_test)
add_executable(gd32f105_exti_backend_test abox_exti_backend_test.c ../hardware/mcu/gd32f105/exti_gd32f105.c)
target_include_directories(gd32f105_exti_backend_test PRIVATE exti_stub ../hardware/api/include ../hardware/internal/include)
add_test(NAME gd32f105_exti_backend_test COMMAND gd32f105_exti_backend_test)
add_executable(abox_hw_clock_test abox_hw_clock_test.c ../hardware/api/src/abox_mcu_clock.c)
target_include_directories(abox_hw_clock_test PRIVATE ../hardware/api/include ../hardware/internal/include ../components/core/include)
add_test(NAME abox_hw_clock_test COMMAND abox_hw_clock_test)
add_executable(abox_gd_clock_variant_test abox_clock_variant_test.c ../hardware/api/src/abox_mcu_clock.c)
target_compile_definitions(abox_gd_clock_variant_test PRIVATE ABOX_MCU_GD32F105=1)
target_include_directories(abox_gd_clock_variant_test PRIVATE ../hardware/api/include ../hardware/internal/include)
add_test(NAME abox_gd_clock_variant_test COMMAND abox_gd_clock_variant_test)
add_executable(stm32_system_clock_test stm32_system_clock_test.c ../hardware/boards/abox_v2/stm32cubemx/src/system_stm32f1xx.c)
target_include_directories(stm32_system_clock_test PRIVATE system_clock_stub)
add_test(NAME stm32_system_clock_test COMMAND stm32_system_clock_test)
add_executable(stm32_system_clock_fraction_test stm32_system_clock_fraction_test.c ../hardware/boards/abox_v2/stm32cubemx/src/system_stm32f1xx.c)
target_include_directories(stm32_system_clock_fraction_test PRIVATE system_clock_stub)
target_compile_definitions(stm32_system_clock_fraction_test PRIVATE HSE_VALUE=25000000U)
add_test(NAME stm32_system_clock_fraction_test COMMAND stm32_system_clock_fraction_test)
add_test(NAME gd32_rcu_fraction_test COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/gd32_rcu_fraction_test.py ${CMAKE_C_COMPILER})
add_executable(stm32f105_clock_backend_test abox_clock_backend_test.c ../hardware/mcu/stm32f105/clock_stm32f105.c)
target_include_directories(stm32f105_clock_backend_test PRIVATE clock_stub ../hardware/api/include ../hardware/internal/include)
target_compile_definitions(stm32f105_clock_backend_test PRIVATE TEST_STM32=1)
add_test(NAME stm32f105_clock_backend_test COMMAND stm32f105_clock_backend_test)
add_executable(gd32f105_clock_backend_test abox_clock_backend_test.c ../hardware/mcu/gd32f105/clock_gd32f105.c)
target_include_directories(gd32f105_clock_backend_test PRIVATE clock_stub ../hardware/api/include ../hardware/internal/include)
add_test(NAME gd32f105_clock_backend_test COMMAND gd32f105_clock_backend_test)
add_executable(abox_time_test abox_time_test.c ../hardware/api/src/abox_time.c)
target_include_directories(abox_time_test PRIVATE ../hardware/api/include ../components/core/include)
add_test(NAME abox_time_test COMMAND abox_time_test)
add_executable(stm32f105_critical_test abox_critical_test.c ../hardware/mcu/critical_cortexm3.c)
target_include_directories(stm32f105_critical_test PRIVATE critical_stub ../hardware/api/include)
add_test(NAME stm32f105_critical_test COMMAND stm32f105_critical_test)
add_executable(gd32f105_critical_test abox_critical_test.c ../hardware/mcu/critical_cortexm3.c)
target_include_directories(gd32f105_critical_test PRIVATE critical_stub ../hardware/api/include)
target_compile_definitions(gd32f105_critical_test PRIVATE ABOX_MCU_GD32F105=1)
add_test(NAME gd32f105_critical_test COMMAND gd32f105_critical_test)
add_executable(abox_flash_guard_test abox_flash_guard_test.c ../hardware/api/src/abox_flash.c ../hardware/mcu/gd32f105/flash_api_gd32f105.c)
target_include_directories(abox_flash_guard_test PRIVATE ../hardware/api/include ../hardware/internal/include ../hardware/mcu/gd32f105/include)
target_link_options(abox_flash_guard_test PRIVATE -Wl,--wrap=ABoxGd32Flash_Begin -Wl,--wrap=ABoxGd32Flash_Write -Wl,--wrap=ABoxGd32Flash_ErasePage)
add_test(NAME abox_flash_guard_test COMMAND abox_flash_guard_test)
add_executable(abox_flash_test abox_flash_test.c ../hardware/api/src/abox_flash.c)
target_include_directories(abox_flash_test PRIVATE ../hardware/api/include ../hardware/internal/include)
add_test(NAME abox_flash_test COMMAND abox_flash_test)
add_executable(abox_i2c_test abox_i2c_test.c ../hardware/api/src/abox_i2c.c)
target_include_directories(abox_i2c_test PRIVATE ../hardware/api/include ../hardware/internal/include ../components/core/include)
add_test(NAME abox_i2c_test COMMAND abox_i2c_test)
add_executable(stm32f105_i2c_backend_test abox_i2c_backend_test.c ../hardware/mcu/stm32f105/i2c_stm32f105.c)
target_include_directories(stm32f105_i2c_backend_test PRIVATE i2c_stub ../hardware/api/include ../hardware/internal/include)
target_compile_definitions(stm32f105_i2c_backend_test PRIVATE TEST_STM32=1)
add_test(NAME stm32f105_i2c_backend_test COMMAND stm32f105_i2c_backend_test)
add_executable(gd32f105_i2c_backend_test abox_i2c_backend_test.c ../hardware/mcu/gd32f105/i2c_gd32f105.c)
target_include_directories(gd32f105_i2c_backend_test PRIVATE i2c_stub ../hardware/api/include ../hardware/internal/include)
add_test(NAME gd32f105_i2c_backend_test COMMAND gd32f105_i2c_backend_test)
add_executable(stm32f105_adc_test abox_adc_test.c ../hardware/mcu/stm32f105/adc_stm32f105.c ../hardware/api/src/abox_adc_watch.c ../hardware/api/src/abox_adc_dual_injected.c)
target_include_directories(stm32f105_adc_test PRIVATE adc_stub ../hardware/api/include ../hardware/internal/include ../components/core/include)
target_compile_definitions(stm32f105_adc_test PRIVATE TEST_STM32=1)
add_test(NAME stm32f105_adc_test COMMAND stm32f105_adc_test)
add_executable(gd32f105_adc_test abox_adc_test.c ../hardware/mcu/gd32f105/adc_gd32f105.c ../hardware/api/src/abox_adc_watch.c ../hardware/api/src/abox_adc_dual_injected.c)
target_include_directories(gd32f105_adc_test PRIVATE adc_stub ../hardware/api/include ../hardware/internal/include ../components/core/include)
add_test(NAME gd32f105_adc_test COMMAND gd32f105_adc_test)
add_executable(stm32f105_timer_test abox_timer_test.c ../hardware/mcu/stm32f105/timer_stm32f105.c)
target_include_directories(stm32f105_timer_test PRIVATE timer_stub ../hardware/api/include ../hardware/internal/include)
target_compile_definitions(stm32f105_timer_test PRIVATE TEST_STM32=1)
add_test(NAME stm32f105_timer_test COMMAND stm32f105_timer_test)
add_executable(gd32f105_timer_test abox_timer_test.c ../hardware/mcu/gd32f105/timer_gd32f105.c)
target_include_directories(gd32f105_timer_test PRIVATE timer_stub ../hardware/api/include ../hardware/internal/include)
add_test(NAME gd32f105_timer_test COMMAND gd32f105_timer_test)
add_executable(stm32f105_dma_test abox_dma_test.c ../hardware/mcu/stm32f105/dma_stm32f105.c ../hardware/api/src/abox_dma_resource.c)
target_include_directories(stm32f105_dma_test PRIVATE dma_stub ../hardware/api/include ../hardware/internal/include)
target_compile_definitions(stm32f105_dma_test PRIVATE TEST_STM32=1)
add_test(NAME stm32f105_dma_test COMMAND stm32f105_dma_test)
add_executable(gd32f105_dma_test abox_dma_test.c ../hardware/mcu/gd32f105/dma_gd32f105.c ../hardware/api/src/abox_dma_resource.c)
target_include_directories(gd32f105_dma_test PRIVATE dma_stub ../hardware/api/include ../hardware/internal/include)
add_test(NAME gd32f105_dma_test COMMAND gd32f105_dma_test)
add_executable(stm32f105_spi_test abox_spi_test.c ../hardware/mcu/stm32f105/spi_stm32f105.c)
target_include_directories(stm32f105_spi_test PRIVATE spi_stub ../hardware/api/include ../hardware/internal/include ../components/core/include)
target_compile_definitions(stm32f105_spi_test PRIVATE TEST_STM32=1)
add_test(NAME stm32f105_spi_test COMMAND stm32f105_spi_test)
add_executable(gd32f105_spi_test abox_spi_test.c ../hardware/mcu/gd32f105/spi_gd32f105.c)
target_include_directories(gd32f105_spi_test PRIVATE spi_stub ../hardware/api/include ../hardware/internal/include ../components/core/include)
add_test(NAME gd32f105_spi_test COMMAND gd32f105_spi_test)
add_executable(abox_spi_crc_test abox_spi_crc_test.c ../hardware/api/src/abox_spi_crc.c)
target_include_directories(abox_spi_crc_test PRIVATE ../hardware/api/include ../hardware/internal/include ../components/core/include)
add_test(NAME abox_spi_crc_test COMMAND abox_spi_crc_test)
add_executable(abox_usb_test abox_usb_test.c ../hardware/api/src/abox_usb.c)
target_include_directories(abox_usb_test PRIVATE ../hardware/api/include ../hardware/internal/include ../components/core/include)
add_test(NAME abox_usb_test COMMAND abox_usb_test)
add_executable(gd32_usb_fifo_test gd32_usb_fifo_test.c ../vendor/gd32f10x/2.7.0/GD32F10x_usbfs_library/Source/drv_usb_core.c)
target_include_directories(gd32_usb_fifo_test PRIVATE usb_stub ../vendor/gd32f10x/2.7.0/GD32F10x_usbfs_library/Include)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # The vendor's fixed 32-bit MMIO addresses are not dereferenced on the host.
    target_compile_options(gd32_usb_fifo_test PRIVATE -Wno-int-to-pointer-cast)
endif()
add_test(NAME gd32_usb_fifo_test COMMAND gd32_usb_fifo_test)
add_executable(abox_i2s_test abox_i2s_test.c ../hardware/api/src/abox_i2s.c)
target_include_directories(abox_i2s_test PRIVATE ../hardware/api/include ../hardware/internal/include ../components/core/include)
add_test(NAME abox_i2s_test COMMAND abox_i2s_test)
add_executable(abox_gd_i2s_test abox_i2s_test.c ../hardware/api/src/abox_i2s.c)
target_include_directories(abox_gd_i2s_test PRIVATE ../hardware/api/include ../hardware/internal/include ../components/core/include)
target_compile_definitions(abox_gd_i2s_test PRIVATE ABOX_MCU_GD32F105=1)
add_test(NAME abox_gd_i2s_test COMMAND abox_gd_i2s_test)
add_executable(abox_wwdg_test abox_wwdg_test.c ../hardware/api/src/abox_wwdg.c)
target_include_directories(abox_wwdg_test PRIVATE ../hardware/api/include ../hardware/internal/include)
add_test(NAME abox_wwdg_test COMMAND abox_wwdg_test)
add_test(NAME abox_wwdg_without_irq_test COMMAND abox_wwdg_test no_irq)
add_test(NAME abox_wwdg_attach_test COMMAND abox_wwdg_test attach)
add_executable(stm32f105_wwdg_backend_test abox_wwdg_backend_test.c ../hardware/mcu/stm32f105/wwdg_stm32f105.c)
target_include_directories(stm32f105_wwdg_backend_test PRIVATE wwdg_stub ../hardware/internal/include)
target_compile_definitions(stm32f105_wwdg_backend_test PRIVATE TEST_STM32=1)
add_test(NAME stm32f105_wwdg_backend_test COMMAND stm32f105_wwdg_backend_test)
add_executable(gd32f105_wwdg_backend_test abox_wwdg_backend_test.c ../hardware/mcu/gd32f105/wwdg_gd32f105.c)
target_include_directories(gd32f105_wwdg_backend_test PRIVATE wwdg_stub ../hardware/internal/include)
add_test(NAME gd32f105_wwdg_backend_test COMMAND gd32f105_wwdg_backend_test)
add_executable(gd32f105_gpio_test abox_gpio_test.c ../hardware/mcu/gd32f105/gpio_gd32f105.c)
target_include_directories(gd32f105_gpio_test PRIVATE gpio_stub ../hardware/api/include)
add_test(NAME gd32f105_gpio_test COMMAND gd32f105_gpio_test)
add_executable(stm32f105_gpio_test abox_gpio_test.c ../hardware/mcu/stm32f105/gpio_stm32f105.c)
target_include_directories(stm32f105_gpio_test PRIVATE gpio_stub ../hardware/api/include)
target_compile_definitions(stm32f105_gpio_test PRIVATE TEST_STM32=1)
add_test(NAME stm32f105_gpio_test COMMAND stm32f105_gpio_test)
add_executable(gd32f105_mcu_reset_test abox_mcu_reset_test.c ../hardware/mcu/gd32f105/mcu_gd32f105.c)
target_include_directories(gd32f105_mcu_reset_test PRIVATE mcu_stub ../hardware/api/include)
add_test(NAME gd32f105_mcu_reset_test COMMAND gd32f105_mcu_reset_test)
add_executable(stm32f105_mcu_reset_test abox_mcu_reset_test.c ../hardware/mcu/stm32f105/mcu_stm32f105.c)
target_include_directories(stm32f105_mcu_reset_test PRIVATE mcu_stub ../hardware/api/include)
target_compile_definitions(stm32f105_mcu_reset_test PRIVATE TEST_STM32=1)
add_test(NAME stm32f105_mcu_reset_test COMMAND stm32f105_mcu_reset_test)
add_executable(abox_boot_v2_installer_test abox_boot_v2_installer_test.c)
target_link_libraries(abox_boot_v2_installer_test PRIVATE abox::boot_v2_boot)
add_test(NAME abox_boot_v2_installer_test COMMAND abox_boot_v2_installer_test)
add_executable(gd32f105_boot_ufs_test gd32f105_boot_ufs_test.c
    ../targets/gd32f105_abox_boot/ufs_at.c)
target_include_directories(gd32f105_boot_ufs_test PRIVATE ../targets/gd32f105_abox_boot)
add_test(NAME gd32f105_boot_ufs_test COMMAND gd32f105_boot_ufs_test)
add_executable(gd32f105_flash_test gd32f105_flash_test.c
    ../hardware/mcu/gd32f105/flash_gd32f105.c)
target_include_directories(gd32f105_flash_test PRIVATE gd32_flash_stub
    ../hardware/mcu/gd32f105/include)
add_test(NAME gd32f105_flash_test COMMAND gd32f105_flash_test)
foreach(mcu IN ITEMS stm32f105 gd32f105)
    add_executable(${mcu}_flash_options_test abox_flash_options_test.c
        ../hardware/api/src/abox_flash_options.c ../hardware/mcu/${mcu}/flash_options_${mcu}.c)
    target_include_directories(${mcu}_flash_options_test PRIVATE flash_options_stub ../hardware/api/include ../hardware/internal/include)
    if(mcu STREQUAL "stm32f105")
        target_compile_definitions(${mcu}_flash_options_test PRIVATE TEST_STM32=1)
    endif()
    add_test(NAME ${mcu}_flash_options_test COMMAND ${mcu}_flash_options_test)
endforeach()
add_executable(gd32f105_watchdog_test gd32f105_watchdog_test.c
    ../hardware/mcu/gd32f105/watchdog_port_gd32f105.c)
target_include_directories(gd32f105_watchdog_test PRIVATE gd_watchdog_stub ../hardware/api/include)
add_test(NAME gd32f105_watchdog_test COMMAND gd32f105_watchdog_test)
add_executable(gd32f105_can_test gd32f105_can_test.c ../hardware/mcu/gd32f105/can_port_gd32f105.c
    ../hardware/api/src/abox_can_board.c ../hardware/boards/abox_v1/board_can_gd32f105.c)
target_include_directories(gd32f105_can_test PRIVATE gd32_can_stub ../hardware/api/include ../hardware/internal/include)
add_test(NAME gd32f105_can_test COMMAND gd32f105_can_test)
add_executable(stm32f105_can_test stm32f105_can_test.c ../hardware/mcu/stm32f105/can_port_stm32f105.c ../hardware/api/src/abox_can_board.c)
target_include_directories(stm32f105_can_test PRIVATE stm32_can_stub ../hardware/api/include ../hardware/internal/include)
add_test(NAME stm32f105_can_test COMMAND stm32f105_can_test)
add_executable(abox_can_events_test abox_can_events_test.c ../hardware/api/src/abox_can_events.c)
target_include_directories(abox_can_events_test PRIVATE ../hardware/api/include)
add_test(NAME abox_can_events_test COMMAND abox_can_events_test)
add_executable(gd32f105_can_events_test gd32f105_can_events_test.c
    ../hardware/api/src/abox_can_events.c ../hardware/mcu/gd32f105/can_events_gd32f105.c)
target_include_directories(gd32f105_can_events_test PRIVATE gd32_can_stub ../hardware/api/include)
add_test(NAME gd32f105_can_events_test COMMAND gd32f105_can_events_test)
add_executable(stm32f105_can_events_test stm32f105_can_events_test.c
    ../hardware/api/src/abox_can_events.c ../hardware/mcu/stm32f105/can_events_stm32f105.c)
target_include_directories(stm32f105_can_events_test PRIVATE stm32_can_stub ../hardware/api/include)
add_test(NAME stm32f105_can_events_test COMMAND stm32f105_can_events_test)
add_executable(gd32f105_v1_board_test gd32f105_v1_board_test.c
    ../hardware/mcu/gd32f105/board_io_gd32f105_v1.c)
target_include_directories(gd32f105_v1_board_test PRIVATE gd32_board_stub
    ../hardware/boards/abox_v1/include)
target_link_libraries(gd32f105_v1_board_test PRIVATE abox::board_io)
add_test(NAME gd32f105_v1_board_test COMMAND gd32f105_v1_board_test)
add_executable(gd32f105_v3_board_test gd32f105_v3_board_test.c
    ../hardware/boards/abox_v3/board_io_v3.c)
target_include_directories(gd32f105_v3_board_test PRIVATE gd32_board_stub ../hardware/api/include)
target_link_libraries(gd32f105_v3_board_test PRIVATE abox::board_io)
add_test(NAME gd32f105_v3_board_test COMMAND gd32f105_v3_board_test)

add_executable(stm32f105_board_startup_test stm32f105_board_startup_test.c
    ../hardware/boards/abox_v2/board_stm32f105.c)
target_include_directories(stm32f105_board_startup_test PRIVATE stm32_board_stub
    ../hardware/api/include ../components/board_io/include ../targets/stm32f105_ec800_v1/include)
add_test(NAME stm32f105_board_startup_test COMMAND stm32f105_board_startup_test)
add_executable(stm32f105_board_can_port_test stm32f105_board_startup_test.c
    ../hardware/boards/abox_v2/board_stm32f105.c)
target_include_directories(stm32f105_board_can_port_test PRIVATE stm32_board_stub
    ../hardware/api/include ../components/board_io/include ../targets/stm32f105_ec800_v1/include)
target_compile_definitions(stm32f105_board_can_port_test PRIVATE ABOX_CAN_PORT_ENABLED=1)
add_test(NAME stm32f105_board_can_port_test COMMAND stm32f105_board_can_port_test)
add_executable(stm32f105_board_serial_disabled_test stm32f105_board_startup_test.c
    ../hardware/boards/abox_v2/board_stm32f105.c)
target_include_directories(stm32f105_board_serial_disabled_test PRIVATE stm32_board_stub
    ../hardware/api/include ../components/board_io/include ../targets/stm32f105_ec800_v1/include)
target_compile_definitions(stm32f105_board_serial_disabled_test PRIVATE ABOX_SERIAL_RX_DISABLED=1)
add_test(NAME stm32f105_board_serial_disabled_test COMMAND stm32f105_board_serial_disabled_test)
add_executable(stm32f105_board_can_integration_test stm32f105_board_startup_test.c
    ../hardware/boards/abox_v2/board_stm32f105.c ../hardware/mcu/stm32f105/can_port_stm32f105.c ../hardware/api/src/abox_can_board.c)
target_include_directories(stm32f105_board_can_integration_test PRIVATE stm32_can_stub stm32_board_stub
    ../hardware/internal/include ../hardware/api/include ../components/board_io/include ../targets/stm32f105_ec800_v1/include)
target_compile_definitions(stm32f105_board_can_integration_test PRIVATE
    ABOX_CAN_PORT_ENABLED=1 ABOX_CAN_PORT_INTEGRATION=1)
add_test(NAME stm32f105_board_can_integration_test COMMAND stm32f105_board_can_integration_test)
add_executable(stm32f105_watchdog_test stm32f105_watchdog_test.c
    ../hardware/mcu/stm32f105/watchdog_port_stm32f105.c)
target_include_directories(stm32f105_watchdog_test PRIVATE stm32_watchdog_stub ../hardware/api/include)
add_test(NAME stm32f105_watchdog_test COMMAND stm32f105_watchdog_test)
add_executable(abox_serial_transaction_test abox_serial_transaction_test.c
    ../hardware/api/src/abox_serial_transaction.c)
target_include_directories(abox_serial_transaction_test PRIVATE ../hardware/api/include)
add_test(NAME abox_serial_transaction_test COMMAND abox_serial_transaction_test)
add_executable(gd32f105_monotonic_test gd32f105_monotonic_test.c)
target_include_directories(gd32f105_monotonic_test PRIVATE gd32_timer_stub
    ../hardware/mcu/gd32f105/include)
add_test(NAME gd32f105_monotonic_test COMMAND gd32f105_monotonic_test)
find_program(ABOX_ARM_GCC arm-none-eabi-gcc)
if(ABOX_ARM_GCC)
    add_test(NAME gd32f105_freertos_vectors COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/gd32f105_freertos_vectors_test.py ${CMAKE_BINARY_DIR})
    add_test(NAME f105_example_build COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/f105_example_build_test.py ${CMAKE_BINARY_DIR})
    add_test(NAME gd32f105_vendor_compile COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/gd32f105_vendor_compile.py
        ${ABOX_ARM_GCC} ${CMAKE_BINARY_DIR})
    add_test(NAME gd32f105_boot_build COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/gd32f105_boot_build_test.py ${CMAKE_BINARY_DIR})
    add_test(NAME stm32f105_boot_suffix_build COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/stm32f105_boot_suffix_build_test.py ${CMAKE_BINARY_DIR})
    add_test(NAME gd32f105_hardware_bench_build COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/gd32f105_uart_bench_build_test.py ${CMAKE_BINARY_DIR})
    add_test(NAME stm32f105_can_vendor_compile COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/stm32f105_can_vendor_compile.py ${ABOX_ARM_GCC} ${CMAKE_BINARY_DIR})
endif()
add_executable(stm32f105_flash_begin_test stm32f105_flash_begin_test.c
    ../hardware/api/src/abox_flash.c ../hardware/mcu/stm32f105/flash_stm32f105.c)
target_include_directories(stm32f105_flash_begin_test PRIVATE
    stm32_port_stub ../hardware/api/include ../hardware/internal/include ../components/core/include ../components/ec_power/include
    ../targets/stm32f105_ec800_v1/include)
add_test(NAME stm32f105_flash_begin_test COMMAND stm32f105_flash_begin_test)
add_executable(abox_mqtt_registry_test abox_mqtt_registry_test.c)
target_link_libraries(abox_mqtt_registry_test PRIVATE abox::mqtt_registry)
add_test(NAME abox_mqtt_registry_test COMMAND abox_mqtt_registry_test)
abox_platform_verify_registry(abox_mqtt_registry_test REGISTRY fixtures/v4/registry-contract.json)
add_executable(abox_mqtt_registry_legacy_test abox_mqtt_registry_legacy_test.c)
target_link_libraries(abox_mqtt_registry_legacy_test PRIVATE abox::mqtt_v4)
add_test(NAME abox_mqtt_registry_legacy_test COMMAND abox_mqtt_registry_legacy_test)
abox_platform_verify_registry(abox_mqtt_registry_legacy_test REGISTRY fixtures/v4/registry-legacy.json LEGACY)
add_test(NAME abox_mqtt_registry_consistency COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/test_mqtt_registry.py
    $<TARGET_FILE:abox_mqtt_registry_test> $<TARGET_FILE:abox_mqtt_registry_legacy_test>)
add_test(NAME abox_mqtt_registry_vectors COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/../tools/validate_mqtt_v4.py
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/v4/registry-contract.json
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/v4/registry-legacy.json)
foreach(case IN ITEMS mqtt_request:mqtt_request cloud_v4:cloud_v4
        mqtt_backoff:mqtt_ec800 time_client:cloud_v4 mqtt_idem_ufs:mqtt_idem_ufs ec800_ufs_at:ec800_ufs_at)
    string(REPLACE ":" ";" pair "${case}")
    list(GET pair 0 name)
    list(GET pair 1 module)
    add_executable(abox_${name}_test abox_${name}_test.c)
    target_link_libraries(abox_${name}_test PRIVATE abox::${module})
    add_test(NAME abox_${name}_test COMMAND abox_${name}_test)
endforeach()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target IN ITEMS abox_mqtt_request abox_mqtt_idempotency abox_mqtt_backoff abox_mqtt_registry
            abox_ec800_ufs_at abox_ufs_journal abox_cloud_v4 abox_mqtt_idem_ufs)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    endforeach()
endif()
add_test(NAME abox_mqtt_psk_files_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_mqtt_psk_files.py)
add_test(NAME abox_jlink_swd_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_jlink_swd.py)
add_test(NAME abox_factory_flash_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_factory_flash.py)
add_test(NAME abox_v4_product_preflight_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_v4_product_preflight.py)
add_executable(abox_monotonic_test abox_monotonic_test.c)
target_include_directories(abox_monotonic_test PRIVATE monotonic_stub
    ../targets/stm32f105_ec800_v1/include)
add_test(NAME abox_monotonic_test COMMAND abox_monotonic_test)
add_executable(abox_time_sync_test abox_time_sync_test.c)
target_link_libraries(abox_time_sync_test PRIVATE abox::core)
add_test(NAME abox_time_sync_test COMMAND abox_time_sync_test)
add_executable(abox_clock_test abox_clock_test.c)
target_link_libraries(abox_clock_test PRIVATE abox::core abox::mqtt_v4)
add_test(NAME abox_clock_test COMMAND abox_clock_test)
add_executable(abox_board_io_test abox_board_io_test.c)
target_link_libraries(abox_board_io_test PRIVATE abox::board_io)
add_test(NAME abox_board_io_test COMMAND abox_board_io_test)
add_executable(abox_board_io_stm32_test abox_board_io_stm32_test.c
    ../targets/stm32f105_ec800_v1/board_io_stm32f105.c)
target_include_directories(abox_board_io_stm32_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stm32_stub
    ${CMAKE_CURRENT_SOURCE_DIR}/../targets/stm32f105_ec800_v1/include)
target_link_libraries(abox_board_io_stm32_test PRIVATE abox::board_io)
add_test(NAME abox_board_io_stm32_test COMMAND abox_board_io_stm32_test)
add_executable(abox_board_io_stm32_v1_test abox_board_io_stm32_test.c
    ../targets/stm32f105_ec800_v1/board_io_stm32f105.c)
target_include_directories(abox_board_io_stm32_v1_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stm32_stub
    ${CMAKE_CURRENT_SOURCE_DIR}/../targets/stm32f105_ec800_v1/include)
target_compile_definitions(abox_board_io_stm32_v1_test PRIVATE ABOX_BOARD_VERSION=1)
target_link_libraries(abox_board_io_stm32_v1_test PRIVATE abox::board_io)
add_test(NAME abox_board_io_stm32_v1_test COMMAND abox_board_io_stm32_v1_test)
add_test(NAME abox_component_contract_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/cmake_component_contract_test.py)
add_executable(abox_rtt_test abox_rtt_test.c)
target_link_libraries(abox_rtt_test PRIVATE abox::rtt)
add_test(NAME abox_rtt_test COMMAND abox_rtt_test)
add_executable(abox_rtt_console_test abox_rtt_console_test.c)
target_link_libraries(abox_rtt_console_test PRIVATE abox::rtt_console)
add_test(NAME abox_rtt_console_test COMMAND abox_rtt_console_test)
add_test(NAME abox_release_publish_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_publish_release.py)
add_executable(abox_config_store_test abox_config_store_test.c)
target_link_libraries(abox_config_store_test PRIVATE abox::core)
add_test(NAME abox_config_store_test COMMAND abox_config_store_test)
foreach(case IN ITEMS
    enrollment:enrollment
    enrollment_ec800_http:enrollment_ec800_http
    ec800_ufs:ec800_ufs
    ec800_tls:ec800_tls
    mqtt_tls:mqtt_tls
    mqtt_trial:mqtt_trial
    ec800_command_port:ec800_command_port
    platform_config:core
    ota_flash:ota
    ota_ec_at:ota
    ec800:ec800
    mqtt_v4_contract:mqtt_v4
    mqtt_runtime:mqtt_runtime
    mqtt_ec800_rx:mqtt_ec800_rx
    mqtt_ec800:mqtt_ec800
    https_ufs_downloader:https_ufs_downloader
    ec_power:ec_power
    boot_v2:boot_v2_boot
    boot_v2_app:boot_v2_app)
    string(REPLACE ":" ";" pair "${case}")
    list(GET pair 0 test_name)
    list(GET pair 1 module)
    if(test_name STREQUAL "ota_flash" OR test_name STREQUAL "ota_ec_at"
            OR test_name STREQUAL "mqtt_v4_contract" OR test_name STREQUAL "ec_power"
            OR test_name STREQUAL "boot_v2" OR test_name STREQUAL "boot_v2_app")
        set(source "${test_name}_test.c")
    else()
        set(source "abox_${test_name}_test.c")
    endif()
    add_executable(abox_${test_name}_test ${source})
    target_link_libraries(abox_${test_name}_test PRIVATE abox::${module})
    add_test(NAME abox_${test_name}_test COMMAND abox_${test_name}_test)
endforeach()
add_executable(abox_ec800_mqtt_rx_route_test abox_ec800_mqtt_rx_route_test.c)
target_link_libraries(abox_ec800_mqtt_rx_route_test PRIVATE abox::ec800 abox::mqtt_ec800_rx)
add_test(NAME abox_ec800_mqtt_rx_route_test COMMAND abox_ec800_mqtt_rx_route_test)
add_executable(abox_mqtt_receipt_test abox_mqtt_receipt_test.c)
target_link_libraries(abox_mqtt_receipt_test PRIVATE abox::mqtt_ec800)
add_test(NAME abox_mqtt_receipt_test COMMAND abox_mqtt_receipt_test)
add_executable(abox_mqtt_ec800_bridge_test abox_mqtt_ec800_bridge_test.c)
target_link_libraries(abox_mqtt_ec800_bridge_test PRIVATE abox::mqtt_ec800)
add_test(NAME abox_mqtt_ec800_bridge_test COMMAND abox_mqtt_ec800_bridge_test)
add_executable(abox_mqtt_runtime_core_port_test abox_mqtt_runtime_core_port_test.c)
target_link_libraries(abox_mqtt_runtime_core_port_test PRIVATE abox::mqtt_runtime)
add_test(NAME abox_mqtt_runtime_core_port_test COMMAND abox_mqtt_runtime_core_port_test)
add_executable(abox_ec800_iccid_test abox_ec800_iccid_test.c)
target_link_libraries(abox_ec800_iccid_test PRIVATE abox::ec800)
add_test(NAME abox_ec800_iccid_test COMMAND abox_ec800_iccid_test)
add_executable(abox_enrollment_identity_test abox_enrollment_identity_test.c)
target_link_libraries(abox_enrollment_identity_test PRIVATE abox::enrollment)
add_test(NAME abox_enrollment_identity_test COMMAND abox_enrollment_identity_test)
add_executable(abox_stm32_enrollment_identity_test
    abox_stm32_enrollment_identity_test.c
    ../targets/stm32f105_ec800_v1/enrollment_identity_stm32f105.c)
target_include_directories(abox_stm32_enrollment_identity_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stm32_identity_stub
    ${CMAKE_CURRENT_SOURCE_DIR}/../targets/stm32f105_ec800_v1/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/boot_v2/include)
target_link_libraries(abox_stm32_enrollment_identity_test PRIVATE abox::enrollment)
add_test(NAME abox_stm32_enrollment_identity_test COMMAND abox_stm32_enrollment_identity_test)
add_executable(abox_enrollment_service_test abox_enrollment_service_test.c)
target_link_libraries(abox_enrollment_service_test PRIVATE abox::app_service)
target_link_libraries(abox_enrollment_service_test PRIVATE abox::enrollment_ec800_http)
add_test(NAME abox_enrollment_service_test COMMAND abox_enrollment_service_test)
target_link_libraries(abox_boot_v2_app_test PRIVATE abox::boot_v2_boot abox::mqtt_idem_nor)

add_executable(abox_boot_v2_ec800_port_test abox_boot_v2_ec800_port_test.c)
target_link_libraries(abox_boot_v2_ec800_port_test PRIVATE abox::boot_v2_app)
add_test(NAME abox_boot_v2_ec800_port_test COMMAND abox_boot_v2_ec800_port_test)
add_executable(abox_boot_v2_app_init_test boot_v2_app_init_test.c)
target_link_libraries(abox_boot_v2_app_init_test PRIVATE abox::boot_v2_app)
add_test(NAME abox_boot_v2_app_init_test COMMAND abox_boot_v2_app_init_test)
add_executable(abox_ec800_recovery_test abox_ec800_recovery_test.c)
target_link_libraries(abox_ec800_recovery_test PRIVATE abox::ec_power)
add_test(NAME abox_ec800_recovery_test COMMAND abox_ec800_recovery_test)
add_executable(abox_mqtt_trial_executor_test abox_mqtt_trial_executor_test.c)
target_link_libraries(abox_mqtt_trial_executor_test PRIVATE abox::mqtt_trial)
add_test(NAME abox_mqtt_trial_executor_test COMMAND abox_mqtt_trial_executor_test)
add_executable(abox_cloud_service_test abox_cloud_service_test.c)
target_link_libraries(abox_cloud_service_test PRIVATE abox::cloud_service)
add_test(NAME abox_cloud_service_test COMMAND abox_cloud_service_test)

add_executable(abox_cloud_internal_v2_test abox_cloud_internal_v2_test.c)
target_link_libraries(abox_cloud_internal_v2_test PRIVATE abox::cloud_service)
add_test(NAME abox_cloud_internal_v2_test COMMAND abox_cloud_internal_v2_test)
add_executable(abox_log_ring_test abox_log_ring_test.c)
target_link_libraries(abox_log_ring_test PRIVATE abox::log_ring)
add_test(NAME abox_log_ring_test COMMAND abox_log_ring_test)

add_executable(abox_cloud_response_test abox_cloud_response_test.c)
target_link_libraries(abox_cloud_response_test PRIVATE abox::cloud_service)
add_test(NAME abox_cloud_response_test COMMAND abox_cloud_response_test)
add_executable(abox_maintenance_service_test abox_maintenance_service_test.c)
target_link_libraries(abox_maintenance_service_test PRIVATE abox::maintenance_service)
add_test(NAME abox_maintenance_service_test COMMAND abox_maintenance_service_test)
add_executable(abox_app_service_test abox_app_service_test.c ../components/cloud/src/abox_app_service.c
    ../components/ec_power/src/abox_ec800_recovery.c)
target_include_directories(abox_app_service_test PRIVATE
    ../components/cloud/include ../components/ec800/include ../components/boot_v2/include
    ../components/ota/include ../components/core/include ../components/ec_power/include)
add_test(NAME abox_app_service_test COMMAND abox_app_service_test)
add_executable(abox_app_service_failure_test app_service_failure_test.c
    ../components/cloud/src/abox_app_service.c ../components/ec_power/src/abox_ec800_recovery.c)
target_include_directories(abox_app_service_failure_test PRIVATE
    ../components/cloud/include ../components/boot_v2/include
    ../components/ota/include ../components/ec_power/include)
target_link_libraries(abox_app_service_failure_test PRIVATE abox::ec800 abox::mqtt_ec800_rx)
add_test(NAME abox_app_service_failure_test COMMAND abox_app_service_failure_test)
add_executable(abox_app_service_uart_test app_service_uart_test.c
    ../components/cloud/src/abox_app_service.c ../components/ec_power/src/abox_ec800_recovery.c)
target_include_directories(abox_app_service_uart_test PRIVATE
    ../components/cloud/include ../components/boot_v2/include ../components/ota/include
    ../components/ec_power/include)
target_link_libraries(abox_app_service_uart_test PRIVATE abox::ec800)
add_test(NAME abox_app_service_uart_test COMMAND abox_app_service_uart_test)
add_executable(abox_registration_failure_test registration_failure_test.c)
target_link_libraries(abox_registration_failure_test PRIVATE abox::mqtt_ec800 abox::enrollment_ec800_http)
add_test(NAME abox_registration_failure_test COMMAND abox_registration_failure_test)
add_executable(abox_cloud_dispatch_queue_test abox_cloud_dispatch_queue_test.c)
target_link_libraries(abox_cloud_dispatch_queue_test PRIVATE abox::cloud_service)
add_test(NAME abox_cloud_dispatch_queue_test COMMAND abox_cloud_dispatch_queue_test)

add_executable(abox_psk_station_test abox_psk_station_test.c
    ../components/ec800/src/abox_psk_station.c)
target_link_libraries(abox_psk_station_test PRIVATE abox::ec800_tls)
add_test(NAME abox_psk_station_test COMMAND abox_psk_station_test)

add_executable(abox_ec800_diagnostics_test abox_ec800_diagnostics_test.c)
target_link_libraries(abox_ec800_diagnostics_test PRIVATE abox::ec800)
add_test(NAME abox_ec800_diagnostics_test COMMAND abox_ec800_diagnostics_test)

add_executable(abox_mqtt_digest_test abox_mqtt_digest_test.c)
target_link_libraries(abox_mqtt_digest_test PRIVATE abox::mqtt_digest)
add_test(NAME abox_mqtt_digest_test COMMAND abox_mqtt_digest_test)
add_test(NAME abox_mqtt_digest_vectors
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_mqtt_digest_vectors.py
        $<TARGET_FILE:abox_mqtt_digest_test>)
add_test(NAME abox_mqtt_numbers
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_mqtt_numbers.py
        $<TARGET_FILE:abox_mqtt_digest_test>)

# Optional adapters from a product checkout; no target/firmware execution.
if(ABOX_STAGE2_PRODUCT_ROOT)
    add_test(NAME product_io_init_test COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_product_io_init.py
        --product-root ${ABOX_STAGE2_PRODUCT_ROOT})
    file(GLOB product_includes "${ABOX_STAGE2_PRODUCT_ROOT}/app/Core/User/*/include")
    add_executable(product_runtime_recovery_test product_runtime_recovery_test.c)
    target_compile_definitions(product_runtime_recovery_test PRIVATE
        PRODUCT_RUNTIME_SOURCE="${ABOX_STAGE2_PRODUCT_ROOT}/app/Core/User/mqtt/src/mqtt_runtime.c")
    if(EXISTS "${ABOX_STAGE2_PRODUCT_ROOT}/app/Core/User/system/include/vcu_config.h")
        target_compile_definitions(product_runtime_recovery_test PRIVATE PRODUCT_SWEEPER=1)
    endif()
    target_include_directories(product_runtime_recovery_test PRIVATE recovery_stubs ${product_includes})
    target_link_libraries(product_runtime_recovery_test PRIVATE abox::mqtt_runtime abox::mqtt_trial abox::core)
    add_test(NAME product_runtime_recovery_test COMMAND product_runtime_recovery_test)
    if(EXISTS "${ABOX_STAGE2_PRODUCT_ROOT}/app/tests/open_idem_recovery_test.c")
        add_executable(product_open_idem_recovery_test
            "${ABOX_STAGE2_PRODUCT_ROOT}/app/tests/open_idem_recovery_test.c")
        target_include_directories(product_open_idem_recovery_test PRIVATE recovery_stubs ${product_includes})
        target_link_libraries(product_open_idem_recovery_test PRIVATE
            abox::ec800 abox::boot_v2_common abox::mqtt_v4)
        add_test(NAME product_open_idem_recovery_test COMMAND product_open_idem_recovery_test)
    endif()
endif()

add_executable(abox_f105_uart_test abox_f105_uart_test.c ../hardware/mcu/uart_f105.c
    ../hardware/api/src/abox_dma_resource.c ../components/ec800/src/abox_ec800_rx.c)
target_include_directories(abox_f105_uart_test PRIVATE ../hardware/api/include ../hardware/internal/include ../components/core/include ../components/ec800/include)
add_test(NAME abox_f105_uart_test COMMAND abox_f105_uart_test)

add_executable(abox_spi_dma_test abox_spi_dma_test.c ../hardware/api/src/abox_spi_dma.c)
target_include_directories(abox_spi_dma_test PRIVATE ../hardware/api/include ../hardware/internal/include ../components/core/include)
add_test(NAME abox_spi_dma_test COMMAND abox_spi_dma_test)

add_executable(abox_adc_dma_test abox_adc_dma_test.c ../hardware/api/src/abox_adc_dma.c)
target_include_directories(abox_adc_dma_test PRIVATE ../hardware/api/include ../hardware/internal/include ../components/core/include)
add_test(NAME abox_adc_dma_test COMMAND abox_adc_dma_test)

add_executable(abox_capture_dma_test abox_capture_dma_test.c ../hardware/api/src/abox_capture_dma.c)
target_include_directories(abox_capture_dma_test PRIVATE ../hardware/api/include ../hardware/internal/include)
add_test(NAME abox_capture_dma_test COMMAND abox_capture_dma_test)

add_executable(abox_pwm_dma_test abox_pwm_dma_test.c ../hardware/api/src/abox_pwm_dma.c)
target_include_directories(abox_pwm_dma_test PRIVATE ../hardware/api/include ../hardware/internal/include)
add_test(NAME abox_pwm_dma_test COMMAND abox_pwm_dma_test)
add_executable(abox_timer_burst_test abox_timer_burst_test.c ../hardware/api/src/abox_timer_burst.c)
target_include_directories(abox_timer_burst_test PRIVATE ../hardware/api/include ../hardware/internal/include)
add_test(NAME abox_timer_burst_test COMMAND abox_timer_burst_test)

add_executable(abox_mqtt_idem_nor_test abox_mqtt_idem_nor_test.c)
target_link_libraries(abox_mqtt_idem_nor_test PRIVATE abox::mqtt_idem_nor)
add_test(NAME abox_mqtt_idem_nor_test COMMAND abox_mqtt_idem_nor_test)
