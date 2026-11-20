"""Build the documented F105 entry and link the real CAN Port for all three boards."""
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[1]
out = pathlib.Path(sys.argv[1])
toolchain = root / "cmake/toolchains/gcc-arm-none-eabi-f105.cmake"
for name, hardware, board, vendor_symbol in (
        ("gd", "gd32f105_ec800_v1", "v1", "can_init"),
        ("st", "stm32f105_ec800_v1", "v2", "HAL_CAN_Init"),
        ("v3", "gd32f105_ec800_v3", "v3", "can_init")):
    build = out / ("f105-example-" + name)
    nor_options = ["-DABOX_EXAMPLE_NOR_IDEM=ON", "-DABOX_EXAMPLE_NOR_OTA=ON",
                   "-DABOX_EXAMPLE_NOR_PROBE=ON"] if board == "v3" else []
    subprocess.run(["cmake", "-S", str(root / "examples/f105_app"), "-B", str(build),
                    "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                    "-DCMAKE_TOOLCHAIN_FILE=" + str(toolchain),
                    "-DABOX_HARDWARE=" + hardware, "-DABOX_BOARD_VERSION=" + board,
                    "-DABOX_EXAMPLE_CAN=ON", "-DABOX_EXAMPLE_CAN_EVENTS=ON",
                    "-DABOX_EXAMPLE_EXTRA_HAL=ON", "-DABOX_EXAMPLE_USB=ON", *nor_options], check=True)
    subprocess.run(["cmake", "--build", str(build)], check=True)
    symbols = subprocess.check_output(["arm-none-eabi-nm", str(build / "ABoxF105Example")], text=True)
    names = {line.split()[-1] for line in symbols.splitlines() if line.split()}
    assert {"ABoxBoard_Init", "ABoxCanPort_Init", "ABoxCanPort_SetFilter", "ABoxCanPort_ReceiveFifo", "ABoxCanPort_DispatchEventIrq", vendor_symbol,
            "ABoxRtc_Open", "ABoxRtc_DispatchEvents", "ABoxRtc_SetCalibration", "ABoxRtc_GetCalibration", "ABoxRtc_SetOutput", "ABoxRtc_GetOutput", "ABoxExti_Open", "ABoxExti_OpenConfigured", "ABoxExti_Trigger", "ABoxExtiInternal_Open", "ABoxExtiInternal_Irq", "ABoxDac_Write", "ABoxSpi_Transfer", "ABoxDma_Start", "ABoxPwm_Configure", "ABoxCapture_Start", "ABoxEncoder_Configure", "ABoxAdc_Read",
            "ABoxI2c_Transfer", "ABoxI2c_Quick", "ABoxI2c_SetSmbusMode", "ABoxI2c_SmbusAlert", "ABoxSmbus_Transfer", "ABoxSmbus_BlockRead", "ABoxSmbus_BlockProcessCall", "ABoxSmbus_Quick", "ABoxSmbus_SetHardwareMode", "ABoxSmbus_AlertPending", "ABoxSmbus_ReadAlertResponse", "ABoxSmbus_HostNotifyStart", "ABoxSmbus_HostNotifyStop", "ABoxSmbus_HostNotifyAbort", "ABoxFlash_Program", "ABoxMcuClock_Apply", "ABoxMcuClock_SetOutput", "ABoxMcuClock_GetOutput", "ABoxMcuClock_SetFailureMonitor", "ABoxMcuClock_FailurePending", "ABoxMcuClock_ClearFailure", "ABoxLowPower_Enter", "ABoxLowPower_RestoreClock", "ABoxLowPower_GetFlags", "ABoxLowPower_ClearFlags", "ABoxLowPower_SetWakePin", "ABoxTimer_SetTrigger", "ABoxTimer_SetOnePulse", "ABoxTimer_SetRepetition",
            "ABoxUart_SendDma", "ABoxUart_ReceiveBlocking", "ABoxUart_Irq", "ABoxSpi_StartDma", "ABoxSpi_TransferCrc", "ABoxSpi_StartDmaCrc", "ABoxUart_DmaIrq", "ABoxI2s_ConfigurePll", "ABoxI2s_ReleasePll", "ABoxI2s_Open", "ABoxI2s_StartDma", "ABoxI2s_StartPolling", "ABoxI2s_PollWrite", "ABoxI2s_PollRead", "ABoxI2s_StopPolling", "ABoxI2s_StartIrq", "ABoxI2s_StopIrq",
            "ABoxI2c_TransferDma", "ABoxAdc_StartDma", "ABoxDac_StartDma", "ABoxCapture_StartDma", "ABoxTimer_SetHallInput",
            "ABoxPwm_StartDma", "ABoxTimer_StartBurstDma", "ABoxTimer_StopBurstDma", "ABoxTimer_Reset", "ABoxAdc_StartDualInjected", "ABoxAdc_DualInjectedReady", "ABoxAdc_FinishDualInjected", "ABoxAdc_AbortDualInjected", "ABoxAdc_TriggerDualLinkedInjected", "ABoxAdc_GetDualLinkedInjected", "ABoxCompare_Configure", "ABoxCompare_Start", "ABoxCompare_Poll", "ABoxCompare_Stop", "ABoxCompare_Release", "ABoxI2c_SlaveStart", "ABoxI2c_Irq", "ABoxTimer_ConfigureBreak", "ABoxTimer_SetBreakEvents", "ABoxTimer_SetSlave",
            "ABoxTimer_RearmOutputs", "ABoxPwm_EnableComplementary", "ABoxPwm_DisableComplementary",
            "ABoxAdc_ReadDual", "ABoxAdc_StartDualDma", "ABoxAdc_StopDualDma", "ABoxAdc_StartInjected", "ABoxAdc_InjectedReady", "ABoxAdc_FinishInjected", "ABoxAdc_AbortInjected",
            "ABoxAdc_WatchConfigure", "ABoxAdc_WatchRead", "ABoxAdc_WatchClear", "ABoxAdc_WatchDisable", "ABoxAdc_Irq", "ABoxAdc_AbortDma", "ABoxAdc_TriggerLinkedInjected", "ABoxAdc_GetLinkedInjected",
            "ABoxWwdg_Start", "ABoxWwdg_Attach", "ABoxWwdg_Refresh", "ABoxWwdg_ReadCounter", "ABoxWwdg_Irq", "ABoxWwdg_SetDebugFreeze",
            "ABoxUsb_Open", "ABoxUsb_Start", "ABoxUsb_Stop", "ABoxUsb_Poll", "ABoxUsbOtg_Enable", "ABoxUsbOtg_EnableManaged", "ABoxUsbOtg_RequestHnp", "ABoxUsbOtg_RequestSrp", "ABoxUsbOtg_Irq", "ABoxUsbOtg_HostSuspend", "ABoxUsbOtg_HostResume", "ABoxUsbDevice_Transfer", "ABoxUsbDevice_Cancel", "ABoxUsbDevice_RemoteWakeup",
            "ABoxUsbHost_OpenPipe", "ABoxUsbHost_Submit", "ABoxUsbHost_Halt",
            "ABoxCrc_Open", "ABoxCrc_UpdateWords", "ABoxCrc_Read", "ABoxBackup_Read", "ABoxBackup_Write", "ABoxBackup_TamperConfigure", "ABoxBackup_TamperRead", "ABoxBackup_TamperClear",
            "ABoxPvd_Open", "ABoxPvd_Irq", "ABoxPvd_Close", "ABoxPinMux_Configure", "ABoxPinMux_SetDebug",
            "ABoxGpio_Lock", "ABoxGpio_GetLocked", "ABoxFlashOptions_ReadActive", "ABoxFlashOptions_Write", "ABoxAdc_AdvanceDualInjected", "ABoxAdc_AbortDualDma", "ABoxTimer_StartBurstDmaOn", "ABoxTimer_SetCaptureDmaOnUpdate"} <= names, name
    if board == "v3":
        assert {"ABoxBoardV3_NorOpen", "ABoxMqttIdemNor_Init", "ABoxBootV2App_Start",
                "ABoxBootV2NorWriter_VerifyTask", "g_abox_example_nor_probe"} <= names
    if name == "st":
        assert "MX_CAN1_Init" not in names and "MX_CAN2_Init" not in names
        assert {"SPI2_IRQHandler", "SPI3_IRQHandler", "WWDG_IRQHandler", "ADC1_2_IRQHandler", "OTG_FS_IRQHandler", "PVD_IRQHandler", "HAL_PCD_Init", "HAL_HCD_Init"} <= names
    else:
        assert {"SPI1_IRQHandler", "SPI2_IRQHandler", "WWDGT_IRQHandler", "ADC0_1_IRQHandler", "USBFS_IRQHandler", "LVD_IRQHandler", "usb_devcore_init", "usb_host_init"} <= names
print("F105 examples linked all three board implementations and all selected peripheral APIs")
