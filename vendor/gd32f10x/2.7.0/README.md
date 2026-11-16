# GD32F10x firmware library 2.7.0: peripherals subset

This directory contains only the vendor CMSIS/device headers and FMC, GPIO,
RCU, USART, DMA, TIMER, CAN, SPI, I2C, ADC, DAC and DBG source,
and the EXTI, RTC and PMU register headers required for the GD32F105 adapters. It is not a complete
GD32 App or Boot target. The upstream files retain their copyright and
license notices. Line endings were normalized from CRLF to LF. The RCU clock
frequency calculation retains fractional intermediate PLL input frequencies
until the final result; the remaining standard peripheral and CMSIS files were
not otherwise changed.

The USBFS controller subset is from the same package. Its local adaptations
bound reset/FIFO waits, limit driver state to the F105's four endpoints and
eight channels, copy exact FIFO tail bytes, guard receive buffers, and avoid
blocking port resets or automatic NAK retries in the interrupt handler. During
managed OTG role adoption, role initialization preserves pending OTG and
role-transition events. Board
VBUS control and upper USB protocol handling are supplied outside the vendor
driver. The original notices remain in every vendor file.

- Vendor: GigaDevice; package `GD32F10x_Firmware_Library_V2.7.0`.
- Source: [official GD32F10x downloads](https://www.gd32mcu.com/en/download?kw=GD32F10X).
- Vendor software license: [SLA-GD0001 version 1.1](https://www.gd32mcu.com/download/agree/box_id/12/document_id/180/path_type/1); the user accepted the download license for this adaptation.
- Outer archive SHA-256: `3494fcb21df8710335cb49dffafd6722c5f2c9dadf5c942bc5369a30ca163b4f`.
- Inner archive SHA-256: `8edcff7227357eafdf9cde138adfb05e0012b27d542afa4bfdabe933290f552b`, verified against the vendor's accompanying `SHA256.txt`.

The source-file and binary-distribution notices in the vendor files must be
retained. Further SDK files must be taken from this same verified package;
upgrades require a separate compatibility review and regression on each
supported hardware target.
