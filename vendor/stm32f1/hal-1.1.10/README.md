# STM32F1 HAL 1.1.10 and CMSIS subset

This is the 70-file STM32F105 driver/CMSIS subset already used by the Meal Delivery Vehicle App. Its CubeMX project identifies STM32Cube FW_F1 V1.8.7; `stm32f1xx_hal.c` identifies HAL version 1.1.10. The files were copied from the existing product checkout, not downloaded anew; line endings were normalized to LF, with no other intentional changes. This is a subset, not the complete STM32Cube package.

The included HAL, STM32 device CMSIS and ARM CMSIS directories retain their original license notices and LICENSE.txt files. Their package notices refer to applicable package terms or fallback licenses; see each original notice.

For reproducibility, SHA-256 over the concatenation of each sorted POSIX relative path (UTF-8), a NUL byte, and the raw SHA-256 digest of that file is `d9e49912e442fb4b98b38c9f8eb4ed30154f50dd136a6223a0c8af27434a1680` for the original 70 files, excluding this README.

The platform CMake function `abox_platform_attach_stm32f105_vendor` supplies these drivers. Products provide their own HAL configuration, startup and system clock and must avoid linking another HAL copy. The existing frozen STM32 Boot retains its own checked-in drivers and build inputs. Vendor updates require a separate compatibility review and regression.
