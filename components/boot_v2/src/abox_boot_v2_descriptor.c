#include "abox_boot_v2.h"
#include "abox_platform_port.h"
#include <stddef.h>

_Static_assert(sizeof(ABoxBootV2Descriptor) == ABOX_BOOT_V2_DESCRIPTOR_SIZE, "Boot descriptor ABI changed");
int ABoxBootV2_DescriptorValid(const ABoxBootV2Descriptor *d){return d&&d->magic==ABOX_BOOT_V2_DESCRIPTOR_MAGIC&&d->abi_version==ABOX_BOOT_V2_DESCRIPTOR_ABI&&d->length==sizeof(*d)&&d->crc32==ABoxBootV2_Crc32(d,(uint32_t)offsetof(ABoxBootV2Descriptor,crc32));}
int ABoxBootV2_DescriptorRead(ABoxBootV2Descriptor *d){return d&&ABox_PortFlashRead(ABOX_BOOT_V2_DESCRIPTOR_ADDR,(uint8_t*)d,sizeof(*d))&&ABoxBootV2_DescriptorValid(d);}
int ABoxBootV2_DescriptorReady(const ABoxBootV2Descriptor *d){return ABoxBootV2_DescriptorReadyFor(d, ABOX_BOOT_V2_FEATURE_UFS_AB);}

int ABoxBootV2_DescriptorReadyFor(const ABoxBootV2Descriptor *d, uint32_t storage_feature)
{
    const uint32_t media = ABOX_BOOT_V2_FEATURE_UFS_AB | ABOX_BOOT_V2_FEATURE_NOR_AB;
    const uint32_t common = ABOX_BOOT_V2_REQUIRED_FEATURES & ~ABOX_BOOT_V2_FEATURE_UFS_AB;
    return (storage_feature == ABOX_BOOT_V2_FEATURE_UFS_AB ||
            storage_feature == ABOX_BOOT_V2_FEATURE_NOR_AB) &&
        ABoxBootV2_DescriptorValid(d) && (d->feature_flags & media) == storage_feature &&
        (d->feature_flags & common) == common;
}
