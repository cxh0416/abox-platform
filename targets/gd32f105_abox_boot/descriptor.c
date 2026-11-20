#include "abox_boot_v2.h"
#include "boot_version.h"

/* This target uses the shared Boot V2 state machine and installer. */
const ABoxBootV2Descriptor g_boot_v2_descriptor __attribute__((used, section(".boot_descriptor"))) = {
    .magic = ABOX_BOOT_V2_DESCRIPTOR_MAGIC, .abi_version = ABOX_BOOT_V2_DESCRIPTOR_ABI,
    .length = sizeof(ABoxBootV2Descriptor),
#ifdef ABOX_GD_BOOT_NOR_AB
    .feature_flags = (ABOX_BOOT_V2_REQUIRED_FEATURES & ~ABOX_BOOT_V2_FEATURE_UFS_AB) |
                     ABOX_BOOT_V2_FEATURE_NOR_AB,
#else
    .feature_flags = ABOX_BOOT_V2_REQUIRED_FEATURES,
#endif
    .semver_major = 2U, .semver_minor = 3U, .semver_patch = 0U,
    .product_id = "abox_gd32f105", .build_version = ABOX_GD_BOOT_VERSION,
#ifdef ABOX_GD_BOOT_NOR_AB
    .crc32 = 0xD126432DU
#else
    .crc32 = 0xF6CDAC9BU
#endif
};
