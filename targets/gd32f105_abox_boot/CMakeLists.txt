cmake_minimum_required(VERSION 3.22)
project(ABox_GD_Boot LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(ABOX_PLATFORM_SCHEDULER BAREMETAL)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.." platform)
set(vendor "${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/gd32f10x/2.7.0")
set(mcu "${CMAKE_CURRENT_SOURCE_DIR}/../../hardware/mcu/gd32f105")
add_executable(ABox_GD_Boot startup.c boot.c descriptor.c syscalls.c ufs_at.c
    "${mcu}/flash_gd32f105.c"
    "${vendor}/GD32F10x_standard_peripheral/Source/gd32f10x_fmc.c"
    "${vendor}/GD32F10x_standard_peripheral/Source/gd32f10x_gpio.c"
    "${vendor}/GD32F10x_standard_peripheral/Source/gd32f10x_rcu.c"
    "${vendor}/GD32F10x_standard_peripheral/Source/gd32f10x_usart.c")
target_include_directories(ABox_GD_Boot PRIVATE "${mcu}/include" "${vendor}/CMSIS"
    "${vendor}/CMSIS/GD/GD32F10x/Include" "${vendor}/GD32F10x_standard_peripheral/Include")
target_compile_definitions(ABox_GD_Boot PRIVATE GD32F10X_CL HXTAL_VALUE=8000000U)
target_compile_options(ABox_GD_Boot PRIVATE -mcpu=cortex-m3 -mthumb -Os -ffunction-sections -fdata-sections -Wall -Wextra)
option(ABOX_GD_BOOT_RAM_SMOKE "RAM-only bench build; all flash mutations disabled" OFF)
option(ABOX_GD_BOOT_FLASH_PROBE "Bench build: test Flash unlock and relock only" OFF)
option(ABOX_GD_BOOT_INSTALL_PROBE "Bench build: reinstall the verified stable App" OFF)
option(ABOX_GD_BOOT_UFS_INFO_PROBE "Bench build: inspect slot sizes without modifying UFS" OFF)
option(ABOX_GD_BOOT_APP_JUMP_PROBE "Bench build: exercise GD App jump without changing state pages" OFF)
if((ABOX_GD_BOOT_RAM_SMOKE AND ABOX_GD_BOOT_FLASH_PROBE) OR
   (ABOX_GD_BOOT_RAM_SMOKE AND ABOX_GD_BOOT_INSTALL_PROBE) OR
   (ABOX_GD_BOOT_FLASH_PROBE AND ABOX_GD_BOOT_INSTALL_PROBE) OR
   (ABOX_GD_BOOT_UFS_INFO_PROBE AND (ABOX_GD_BOOT_FLASH_PROBE OR ABOX_GD_BOOT_INSTALL_PROBE)) OR
   (ABOX_GD_BOOT_APP_JUMP_PROBE AND (ABOX_GD_BOOT_RAM_SMOKE OR ABOX_GD_BOOT_FLASH_PROBE OR
    ABOX_GD_BOOT_INSTALL_PROBE OR ABOX_GD_BOOT_UFS_INFO_PROBE)))
    message(FATAL_ERROR "Boot bench modes must not be combined")
endif()
if(ABOX_GD_BOOT_FLASH_PROBE)
    target_compile_definitions(ABox_GD_Boot PRIVATE ABOX_GD_BOOT_FLASH_PROBE=1)
endif()
if(ABOX_GD_BOOT_INSTALL_PROBE)
    target_compile_definitions(ABox_GD_Boot PRIVATE ABOX_GD_BOOT_INSTALL_PROBE=1)
    target_sources(ABox_GD_Boot PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/gd32_boot_install_probe.c")
    target_link_options(ABox_GD_Boot PRIVATE -Wl,--wrap=ABoxGd32Flash_ErasePage -Wl,--wrap=ABoxGd32Flash_Write)
endif()
if(ABOX_GD_BOOT_UFS_INFO_PROBE)
    target_compile_definitions(ABox_GD_Boot PRIVATE ABOX_GD_BOOT_UFS_INFO_PROBE=1)
endif()
if(ABOX_GD_BOOT_APP_JUMP_PROBE)
    target_sources(ABox_GD_Boot PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/gd32_boot_jump_probe.c")
    target_link_options(ABox_GD_Boot PRIVATE -Wl,--wrap=ABoxBootV2_StateLoad)
endif()
set(boot_linker_script "${CMAKE_CURRENT_SOURCE_DIR}/boot.ld")
if(ABOX_GD_BOOT_RAM_SMOKE)
    target_compile_definitions(ABox_GD_Boot PRIVATE ABOX_GD_BOOT_RAM_SMOKE=1)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${boot_linker_script}")
    file(READ "${boot_linker_script}" ram_script)
    string(REPLACE "ORIGIN = 0x08000000, LENGTH = 0x7F00" "ORIGIN = 0x20000000, LENGTH = 0x4F00" ram_script "${ram_script}")
    string(REPLACE "ORIGIN = 0x08007F00" "ORIGIN = 0x20004F00" ram_script "${ram_script}")
    string(REPLACE "ORIGIN = 0x20000000, LENGTH = 0xBC40" "ORIGIN = 0x20005000, LENGTH = 0x6C40" ram_script "${ram_script}")
    set(boot_linker_script "${CMAKE_CURRENT_BINARY_DIR}/boot-ram.ld")
    file(WRITE "${boot_linker_script}" "${ram_script}")
    target_sources(ABox_GD_Boot PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/gd32_boot_ram_guard.c")
    target_link_options(ABox_GD_Boot PRIVATE -Wl,--wrap=ABoxGd32Flash_Begin
        -Wl,--wrap=ABoxGd32Flash_Write -Wl,--wrap=ABoxGd32Flash_ErasePage)
endif()
target_link_options(ABox_GD_Boot PRIVATE -mcpu=cortex-m3 -mthumb --specs=nano.specs --specs=nosys.specs
    "-T${boot_linker_script}" -Wl,--gc-sections -Wl,--print-memory-usage)
set_property(TARGET ABox_GD_Boot APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/boot.ld")
target_link_libraries(ABox_GD_Boot PRIVATE abox::boot_v2_boot)
if(ABOX_GD_BOOT_RAM_SMOKE OR ABOX_GD_BOOT_FLASH_PROBE OR ABOX_GD_BOOT_INSTALL_PROBE OR
   ABOX_GD_BOOT_UFS_INFO_PROBE OR ABOX_GD_BOOT_APP_JUMP_PROBE)
    set_target_properties(ABox_GD_Boot PROPERTIES OUTPUT_NAME ABox_GD_Boot_Bench)
endif()
add_custom_command(TARGET ABox_GD_Boot POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:ABox_GD_Boot> $<TARGET_FILE_BASE_NAME:ABox_GD_Boot>.bin)
