#ifndef ABOX_GD_BOOT_UFS_AT_H
#define ABOX_GD_BOOT_UFS_AT_H
#include <stdint.h>
/* Receive returns 1 for a byte, 0 for timeout and -1 for transport error. */
uint32_t ABoxGdBootTick(void);
int ABoxGdBootUartWrite(void *context, const uint8_t *data, uint32_t length);
int ABoxGdBootUartRead(uint8_t *data, uint32_t timeout);
int ABoxGdBootUfs_Init(void);
int ABoxGdBootUfs_Open(void *context, uint8_t slot);
int ABoxGdBootUfs_Read(void *context, uint8_t *data, uint32_t length);
int ABoxGdBootUfs_Close(void *context);
#if defined(ABOX_GD_BOOT_UFS_INFO_PROBE)
int ABoxGdBootUfs_GetSize(uint8_t slot, uint32_t *size);
#endif
#endif
