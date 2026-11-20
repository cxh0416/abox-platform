#ifndef ABOX_BOOT_V2_H
#define ABOX_BOOT_V2_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ABOX_BOOT_V2_STATE_MAGIC 0x42535632U
#define ABOX_BOOT_V2_STATE_VERSION 3U
#define ABOX_BOOT_V2_DESCRIPTOR_MAGIC 0x32564241U
#define ABOX_BOOT_V2_DESCRIPTOR_ABI 1U
#define ABOX_BOOT_V2_DESCRIPTOR_SIZE 256U
#define ABOX_BOOT_V2_DESCRIPTOR_ADDR 0x08007F00U

#define ABOX_BOOT_V2_FEATURE_STATE_V3       (1UL << 0)
#define ABOX_BOOT_V2_FEATURE_UFS_AB         (1UL << 1)
#define ABOX_BOOT_V2_FEATURE_APP_STAGING    (1UL << 2)
#define ABOX_BOOT_V2_FEATURE_ROLLBACK_REPORT (1UL << 3)
#define ABOX_BOOT_V2_FEATURE_NOR_AB         (1UL << 4)
#define ABOX_BOOT_V2_REQUIRED_FEATURES      0x0000000FUL

#define ABOX_BOOT_V2_ERROR_NONE 0U
#define ABOX_BOOT_V2_ERROR_CANDIDATE_INVALID 1U
#define ABOX_BOOT_V2_ERROR_INSTALL_IO 2U
#define ABOX_BOOT_V2_ERROR_INSTALL_CRC 3U
#define ABOX_BOOT_V2_ERROR_TRIAL_FAULT 4U
#define ABOX_BOOT_V2_ERROR_ROLLED_BACK 5U

typedef enum {
    ABOX_BOOT_V2_NORMAL = 0,
    ABOX_BOOT_V2_INSTALL_PENDING,
    ABOX_BOOT_V2_INSTALLING,
    ABOX_BOOT_V2_TRIAL,
    ABOX_BOOT_V2_CONFIRMED,
    ABOX_BOOT_V2_ROLLBACK
} ABoxBootV2StateCode;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t sequence;
    uint32_t state;
    uint8_t stable_slot;
    uint8_t candidate_slot;
    uint8_t trial_fail_count;
    uint8_t last_reset_reason;
    uint32_t image_size;
    uint32_t image_crc32;
    char image_version[32];
    uint32_t stable_image_size;
    uint32_t stable_image_crc32;
    char stable_image_version[32];
    uint32_t last_error;
    uint8_t rollback_report_pending;
    uint8_t rollback_installed;
    uint8_t reserved[2];
    uint32_t record_crc32;
} ABoxBootV2Record;

typedef struct {
    uint8_t page_a_valid;
    uint8_t page_b_valid;
    uint8_t selected_page;
    uint8_t selected_valid;
    uint32_t page_a_sequence;
    uint32_t page_b_sequence;
    uint32_t selected_sequence;
} ABoxBootV2Diagnostics;

typedef struct {
    uint32_t state_a_addr;
    uint32_t state_b_addr;
    uint32_t page_size;
    uint32_t app_start_addr;
    uint32_t app_end_addr;
    uint32_t sram_start_addr;
    uint32_t sram_end_addr;
} ABoxBootV2Layout;

typedef struct {
    uint32_t magic;
    uint16_t abi_version;
    uint16_t length;
    uint32_t feature_flags;
    uint16_t semver_major;
    uint16_t semver_minor;
    uint16_t semver_patch;
    uint16_t reserved0;
    char product_id[32];
    char build_version[64];
    uint8_t reserved[136];
    uint32_t crc32;
} ABoxBootV2Descriptor;

typedef struct {
    uint32_t magic;
    uint32_t reason;
    uint32_t pc;
    uint32_t lr;
    uint32_t crc32;
} ABoxBootV2FaultMailbox;

uint32_t ABoxBootV2_Crc32(const void *data, uint32_t length);
int ABoxBootV2_StateBindLayout(const ABoxBootV2Layout *layout);
int ABoxBootV2_StateLoad(ABoxBootV2Record *record);
int ABoxBootV2_StateSave(const ABoxBootV2Record *record);
int ABoxBootV2_StateDiagnostics(ABoxBootV2Diagnostics *diagnostics);
int ABoxBootV2_Confirm(void);
int ABoxBootV2_AcknowledgeRollback(void);
const char *ABoxBootV2_StateName(uint32_t state);
int ABoxBootV2_ImageVectorValid(const uint8_t vector[8]);
int ABoxBootV2_DescriptorRead(ABoxBootV2Descriptor *descriptor);
int ABoxBootV2_DescriptorValid(const ABoxBootV2Descriptor *descriptor);
int ABoxBootV2_DescriptorReady(const ABoxBootV2Descriptor *descriptor);
int ABoxBootV2_DescriptorReadyFor(const ABoxBootV2Descriptor *descriptor, uint32_t storage_feature);
void ABoxBootV2_FaultRecord(uint32_t reason, uint32_t pc, uint32_t lr);
int ABoxBootV2_FaultValid(void);
void ABoxBootV2_FaultClear(void);
const volatile ABoxBootV2FaultMailbox *ABoxBootV2_FaultMailboxGet(void);

#ifdef __cplusplus
}
#endif
#endif
