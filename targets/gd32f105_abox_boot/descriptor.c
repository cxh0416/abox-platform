#include "abox_boot_v2.h"
#include "boot_version.h"

/* Capability flags remain unset until installation and rollback are verified. */
const ABoxBootV2Descriptor g_boot_v2_descriptor __attribute__((used, section(".boot_descriptor"))) = {
    .magic = ABOX_BOOT_V2_DESCRIPTOR_MAGIC, .abi_version = ABOX_BOOT_V2_DESCRIPTOR_ABI,
    .length = sizeof(ABoxBootV2Descriptor), .feature_flags = 0U,
    .semver_major = 1U, .semver_minor = 0U, .semver_patch = 0U,
    .product_id = "abox_gd32f105", .build_version = ABOX_GD_BOOT_VERSION,
    .crc32 = 0xC2FC3849U
};
