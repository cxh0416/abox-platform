cmake_minimum_required(VERSION 3.22)
project(ABoxF105Example LANGUAGES C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(ABOX_HARDWARE "" CACHE STRING "Explicit platform hardware target")
option(ABOX_EXAMPLE_CAN "Compile the public CAN silent-loopback example" ON)
option(ABOX_EXAMPLE_CAN_EVENTS "Compile the public CAN event service" OFF)
option(ABOX_EXAMPLE_EXTRA_HAL "Link optional peripheral APIs for integration verification" OFF)
option(ABOX_EXAMPLE_USB "Link USB controller for integration verification" OFF)
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
    target_link_options(ABoxF105Example PRIVATE "-Wl,--undefined=ABoxCanPort_SetFilter" "-Wl,--undefined=ABoxCanPort_ReceiveFifo")
endif()
if(ABOX_EXAMPLE_CAN_EVENTS)
    list(APPEND example_can CAN_EVENTS)
endif()
abox_platform_attach_f105_app(ABoxF105Example
    HARDWARE "${ABOX_HARDWARE}"
    PRODUCT_CONFIG_DIR "${CMAKE_CURRENT_LIST_DIR}/config"
    ${example_can})
option(ABOX_EXAMPLE_NOR_IDEM "Mount the V3 MQTT V4 idempotency log" OFF)
option(ABOX_EXAMPLE_NOR_OTA "Link the V3 NOR OTA App path and check the Boot descriptor" OFF)
option(ABOX_EXAMPLE_NOR_PROBE "Bench: erase and test the reserved V3 NOR sector" OFF)
if(ABOX_EXAMPLE_NOR_IDEM OR ABOX_EXAMPLE_NOR_OTA OR ABOX_EXAMPLE_NOR_PROBE)
    if(NOT ABOX_HARDWARE STREQUAL "gd32f105_ec800_v3")
        message(FATAL_ERROR "The reference NOR options require the V3 hardware contract")
    endif()
    abox_platform_attach_v3_nor(ABoxF105Example)
    target_compile_definitions(ABoxF105Example PRIVATE ABOX_EXAMPLE_NOR=1)
endif()
if(ABOX_EXAMPLE_NOR_PROBE)
    target_compile_definitions(ABoxF105Example PRIVATE ABOX_EXAMPLE_NOR_PROBE=1)
endif()
if(ABOX_EXAMPLE_NOR_IDEM)
    target_compile_definitions(ABoxF105Example PRIVATE ABOX_EXAMPLE_NOR_IDEM=1)
    target_link_libraries(ABoxF105Example abox::mqtt_idem_nor)
endif()
if(ABOX_EXAMPLE_NOR_OTA)
    target_compile_definitions(ABoxF105Example PRIVATE ABOX_EXAMPLE_NOR_OTA=1)
    target_link_options(ABoxF105Example PRIVATE
        "-Wl,--undefined=ABoxBootV2App_Init" "-Wl,--undefined=ABoxBootV2App_BeginProvisioning"
        "-Wl,--undefined=ABoxBootV2App_Start" "-Wl,--undefined=ABoxBootV2App_Task"
        "-Wl,--undefined=ABoxBootV2_Confirm")
endif()
if(ABOX_EXAMPLE_USB)
    abox_platform_attach_usb(ABoxF105Example HARDWARE "${ABOX_HARDWARE}")
    foreach(symbol IN ITEMS ABoxUsb_Open ABoxUsb_Start ABoxUsb_Stop ABoxUsb_Close ABoxUsb_Irq ABoxUsb_Poll
        ABoxUsbDevice_OpenEndpoint ABoxUsbDevice_CloseEndpoint ABoxUsbDevice_Transfer ABoxUsbDevice_Cancel ABoxUsbDevice_SetStall ABoxUsbDevice_SetAddress ABoxUsbDevice_RemoteWakeup
        ABoxUsbHost_ResetPort ABoxUsbHost_OpenPipe ABoxUsbHost_ClosePipe ABoxUsbHost_Submit ABoxUsbHost_Halt ABoxUsbHost_PortSpeed ABoxUsbHost_Frame)
        target_link_options(ABoxF105Example PRIVATE "-Wl,--undefined=${symbol}")
    endforeach()
endif()

# Optional link verification: retain each public entry even with section GC,
# without initializing unused peripherals or touching pins in the example.
if(ABOX_EXAMPLE_EXTRA_HAL)
    foreach(peripheral IN ITEMS spi i2s dma timer adc dac i2c exti rtc wwdg crc backup pvd pinmux low_power smbus usb_otg flash_options)
        cmake_language(CALL abox_platform_attach_${peripheral} ABoxF105Example HARDWARE "${ABOX_HARDWARE}")
    endforeach()
    set(retain_flags "")
    foreach(symbol IN ITEMS ABoxFlashOptions_ReadActive ABoxFlashOptions_Write ABoxPinMux_Configure ABoxPinMux_SetDebug ABoxCrc_Open ABoxCrc_Reset ABoxCrc_UpdateWords ABoxCrc_Read ABoxCrc_WriteScratch ABoxCrc_ReadScratch ABoxCrc_Close
            ABoxBackup_Read ABoxBackup_Write ABoxBackup_TamperConfigure ABoxBackup_TamperRead ABoxBackup_TamperClear ABoxPvd_Open ABoxPvd_Read ABoxPvd_Pending ABoxPvd_Clear ABoxPvd_Irq ABoxPvd_Close
            ABoxRtc_Open ABoxRtc_Close ABoxRtc_Read ABoxRtc_Write ABoxRtc_SetAlarm
            ABoxRtc_GetFlags ABoxRtc_ClearFlags ABoxRtc_SetInterrupts ABoxRtc_DispatchEvents ABoxRtc_SetCalibration ABoxRtc_GetCalibration ABoxRtc_SetOutput ABoxRtc_GetOutput ABoxRtc_ResetBackupDomain
            ABoxExti_Open ABoxExti_OpenConfigured ABoxExti_Close ABoxExti_Trigger ABoxExti_Irq
            ABoxExtiInternal_Open ABoxExtiInternal_Close ABoxExtiInternal_Trigger ABoxExtiInternal_Pending ABoxExtiInternal_Clear ABoxExtiInternal_Irq
            ABoxUart_SendDma ABoxUart_ReceiveBlocking ABoxUart_SetMute ABoxUart_IsMuted ABoxUart_Irq ABoxUart_DmaIrq
            ABoxDac_Open ABoxDac_Enable ABoxDac_Disable ABoxDac_Close ABoxDac_Write ABoxDac_Trigger ABoxDac_ReadOutput ABoxDac_StartDma ABoxDac_StopDma
            ABoxSpi_Open ABoxSpi_Close ABoxSpi_Transfer ABoxSpi_StartDma ABoxSpi_FinishDma ABoxSpi_AbortDma
            ABoxSpi_TransferCrc ABoxSpi_StartDmaCrc ABoxI2s_ConfigurePll ABoxI2s_ReleasePll ABoxI2s_Open ABoxI2s_Close ABoxI2s_StartDma ABoxI2s_StopDma ABoxI2s_AbortDma ABoxI2s_Irq
            ABoxI2s_StartPolling ABoxI2s_PollWrite ABoxI2s_PollRead ABoxI2s_StopPolling ABoxI2s_StartIrq ABoxI2s_StopIrq
            ABoxWwdg_Start ABoxWwdg_Attach ABoxWwdg_Refresh ABoxWwdg_ReadCounter ABoxWwdg_Irq ABoxWwdg_SetDebugFreeze
            ABoxDma_Open ABoxDma_Start ABoxDma_Abort ABoxDma_Close ABoxDma_Remaining ABoxDma_Irq
            ABoxTimer_Open ABoxTimer_Start ABoxTimer_Stop ABoxTimer_Close ABoxTimer_Reset ABoxTimer_Read
            ABoxTimer_SetCounter ABoxTimer_SetOnePulse ABoxTimer_SetRepetition ABoxTimer_SetTrigger ABoxTimer_SetCaptureDmaOnUpdate ABoxTimer_SetSlave ABoxTimer_SetHallInput ABoxTimer_Irq ABoxTimer_ConfigureBreak ABoxTimer_BreakPending ABoxTimer_ClearBreak ABoxTimer_SetBreakEvents ABoxTimer_OutputsArmed ABoxTimer_RearmOutputs
            ABoxPwm_Configure ABoxPwm_Enable ABoxPwm_Disable ABoxPwm_Release ABoxPwm_StartDma ABoxPwm_StopDma ABoxPwm_ConfigureAdvancedOutput ABoxPwm_EnableComplementary ABoxPwm_DisableComplementary
            ABoxTimer_StartBurstDma ABoxTimer_StartBurstDmaOn ABoxTimer_StopBurstDma
            ABoxCompare_Configure ABoxCompare_Start ABoxCompare_Poll ABoxCompare_Stop ABoxCompare_Release
            ABoxCapture_Configure ABoxCapture_Start ABoxCapture_Read ABoxCapture_Stop ABoxCapture_Release ABoxCapture_StartDma ABoxCapture_StopDma
            ABoxEncoder_Configure ABoxEncoder_Release
            ABoxAdc_Open ABoxAdc_Close ABoxAdc_WatchConfigure ABoxAdc_WatchRead ABoxAdc_WatchClear ABoxAdc_WatchDisable ABoxAdc_Irq ABoxAdc_Read ABoxAdc_ReadDual ABoxAdc_StartDma ABoxAdc_StopDma ABoxAdc_AbortDma ABoxAdc_TriggerLinkedInjected ABoxAdc_GetLinkedInjected ABoxAdc_StartDualDma ABoxAdc_StopDualDma ABoxAdc_StartInjected ABoxAdc_InjectedReady ABoxAdc_FinishInjected ABoxAdc_AbortInjected ABoxAdc_StartDualInjected ABoxAdc_DualInjectedReady ABoxAdc_FinishDualInjected ABoxAdc_AbortDualInjected
            ABoxAdc_AdvanceDualInjected ABoxAdc_AbortDualDma ABoxAdc_TriggerDualLinkedInjected ABoxAdc_GetDualLinkedInjected
            ABoxI2c_Open ABoxI2c_Close ABoxI2c_SetSmbusMode ABoxI2c_SmbusAlert ABoxI2c_Transfer ABoxI2c_Quick ABoxI2c_TransferDma ABoxI2c_SlaveStart ABoxI2c_SlaveStop ABoxI2c_SlaveAbort ABoxI2c_Irq ABoxSmbus_Transfer ABoxSmbus_BlockRead ABoxSmbus_BlockProcessCall ABoxSmbus_Quick ABoxSmbus_SetHardwareMode ABoxSmbus_AlertPending ABoxSmbus_ReadAlertResponse ABoxSmbus_HostNotifyStart ABoxSmbus_HostNotifyStop ABoxSmbus_HostNotifyAbort
            ABoxGpio_Lock ABoxGpio_GetLocked ABoxFlash_Geometry ABoxFlash_Begin ABoxFlash_Program ABoxFlash_ErasePage
            ABoxFlash_End ABoxFlash_Read ABoxTime_TickMs ABoxTime_MonotonicMs ABoxTime_DelayMs ABoxCritical_Enter ABoxCritical_Exit
            ABoxMcuClock_Apply ABoxMcuClock_Get ABoxMcuClock_SetOutput ABoxMcuClock_GetOutput ABoxMcuClock_SetFailureMonitor ABoxMcuClock_FailurePending ABoxMcuClock_ClearFailure
            ABoxLowPower_Enter ABoxLowPower_RestoreClock ABoxLowPower_GetFlags ABoxLowPower_ClearFlags ABoxLowPower_SetWakePin
            ABoxUsbOtg_Enable ABoxUsbOtg_EnableManaged ABoxUsbOtg_Disable ABoxUsbOtg_GetStatus ABoxUsbOtg_SetHnpPermission ABoxUsbOtg_RequestHnp ABoxUsbOtg_RequestSrp ABoxUsbOtg_Irq ABoxUsbOtg_HostSuspend ABoxUsbOtg_HostResume)
        string(APPEND retain_flags "-Wl,--undefined=${symbol}\n")
    endforeach()
    # Keep the Windows command line bounded as the public API list grows.
    set(retain_file "${CMAKE_CURRENT_BINARY_DIR}/abox_retain_symbols.rsp")
    file(GENERATE OUTPUT "${retain_file}" CONTENT "${retain_flags}")
    target_link_options(ABoxF105Example PRIVATE "@${retain_file}")
    set_property(TARGET ABoxF105Example APPEND PROPERTY LINK_DEPENDS "${retain_file}")
endif()
