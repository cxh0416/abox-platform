#include "abox_boot_v2.h"
#ifdef ABOX_BOOT_DESCRIPTOR_ST_SUFFIX
#define ABOX_BOOT_DESCRIPTOR_VERSION "abox-boot-2.3.0-st"
#define ABOX_BOOT_DESCRIPTOR_CRC 0xD1929276U
#else
#define ABOX_BOOT_DESCRIPTOR_VERSION "abox-boot-2.3.0"
#define ABOX_BOOT_DESCRIPTOR_CRC 0xE55BBFCBU
#endif
const ABoxBootV2Descriptor g_boot_v2_descriptor __attribute__((used,section(".boot_descriptor")))={
 .magic=ABOX_BOOT_V2_DESCRIPTOR_MAGIC,.abi_version=ABOX_BOOT_V2_DESCRIPTOR_ABI,.length=sizeof(ABoxBootV2Descriptor),
 .feature_flags=ABOX_BOOT_V2_REQUIRED_FEATURES,.semver_major=2U,.semver_minor=3U,.semver_patch=0U,
 .product_id="abox_stm32f105",.build_version=ABOX_BOOT_DESCRIPTOR_VERSION,.crc32=ABOX_BOOT_DESCRIPTOR_CRC};
