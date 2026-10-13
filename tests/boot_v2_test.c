#include "abox_boot_v2.h"
#include "abox_boot_v2_boot.h"
#include "abox_platform_port.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define BASE 0x08000000U
static uint8_t flash_mem[0x40000];
static int write_fail_after=-1,write_calls;
static uint8_t image_valid=1U,reset_value;
static ABoxBootV2InstallResult install_result=ABOX_BOOT_V2_INSTALL_OK;
static uint32_t install_calls,jump_calls;
static uint8_t match_image_metadata;
static uint32_t flash_image_size,flash_image_crc;
static int begin(void*c){(void)c;return 1;}static void end(void*c){(void)c;}
static int read_flash(void*c,uint32_t a,uint8_t*d,uint32_t n){(void)c;if(a<BASE||a-BASE+n>sizeof(flash_mem))return 0;memcpy(d,flash_mem+a-BASE,n);return 1;}
static int write_flash(void*c,uint32_t a,const uint8_t*d,uint32_t n){(void)c;if(write_fail_after>=0&&write_calls++>=write_fail_after)return 0;if(a<BASE||a-BASE+n>sizeof(flash_mem))return 0;memcpy(flash_mem+a-BASE,d,n);return 1;}
static int erase(void*c,uint32_t a){(void)c;if(a<BASE||a-BASE+0x800U>sizeof(flash_mem))return 0;memset(flash_mem+a-BASE,0xFF,0x800U);return 1;}
static uint32_t tick(void*c){(void)c;return 0;}static int uart(void*c,const uint8_t*d,uint32_t n){(void)c;(void)d;return n?1:0;}
static void critical(void*c){(void)c;}
static uint8_t boot_flash_valid(void*c,uint32_t size,uint32_t crc){(void)c;return image_valid&&(!match_image_metadata||(size==flash_image_size&&crc==flash_image_crc));}
static ABoxBootV2InstallResult boot_install(void*c,uint8_t slot,uint32_t size,uint32_t crc){(void)c;(void)slot;install_calls++;if(install_result==ABOX_BOOT_V2_INSTALL_OK){flash_image_size=size;flash_image_crc=crc;}return install_result;}
static uint8_t boot_vector(void*c){(void)c;return 1U;}
static void boot_jump(void*c){(void)c;jump_calls++;}
static uint8_t boot_reset(void*c){(void)c;return reset_value;}
static void boot_delay(void*c,uint32_t ms){(void)c;(void)ms;}
int main(void)
{
    static const ABoxFlashLayout legacy={0x08008000U,0x0803F800U,0x800U};
    static const ABoxPlatformPort port={0,tick,uart,begin,write_flash,erase,end,critical,critical,0,0,&legacy,0,0,read_flash};
    static const ABoxBootV2Layout layout={0x0803E800U,0x0803F000U,0x800U,0x08008000U,0x0803E7FFU,0x20000000U,0x20010000U};
    static const ABoxBootV2BootPort boot_port={0,boot_flash_valid,boot_install,boot_vector,boot_jump,boot_reset,boot_delay,0};
    ABoxBootV2Record r,out; ABoxBootV2Diagnostics d; ABoxBootV2Descriptor desc; uint8_t vector[8]; uint32_t sp,reset;
    memset(flash_mem,0xFF,sizeof(flash_mem));assert(sizeof(ABoxBootV2Record)==112U);assert(sizeof(ABoxBootV2Descriptor)==256U);
    assert(ABox_PlatformPortBind(&port));assert(ABoxBootV2_StateBindLayout(&layout));
    sp=layout.sram_end_addr;reset=layout.app_start_addr+1U;memcpy(vector,&sp,4U);memcpy(vector+4U,&reset,4U);assert(ABoxBootV2_ImageVectorValid(vector));
    sp=layout.sram_end_addr+4U;memcpy(vector,&sp,4U);assert(!ABoxBootV2_ImageVectorValid(vector));
    memset(&r,0,sizeof(r));r.state=ABOX_BOOT_V2_CONFIRMED;r.stable_slot=0U;r.stable_image_size=16U;r.stable_image_crc32=0x12345678U;assert(ABoxBootV2_StateSave(&r));assert(ABoxBootV2_StateLoad(&out));assert(out.sequence==1U&&out.state==ABOX_BOOT_V2_CONFIRMED);
    r=out;r.state=ABOX_BOOT_V2_TRIAL;r.candidate_slot=1U;r.image_size=32U;r.image_crc32=0x89ABCDEFU;assert(ABoxBootV2_StateSave(&r));assert(ABoxBootV2_StateDiagnostics(&d));assert(d.page_a_valid&&d.page_b_valid&&d.selected_sequence==2U);
    assert(ABoxBootV2_Confirm());assert(ABoxBootV2_StateLoad(&out));assert(out.state==ABOX_BOOT_V2_CONFIRMED&&out.stable_slot==1U);
    flash_mem[layout.state_a_addr-BASE]^=0x01U;assert(ABoxBootV2_StateLoad(&out));assert(out.sequence>=2U);
    r=out;r.last_error=99U;write_fail_after=5;write_calls=0;assert(!ABoxBootV2_StateSave(&r));write_fail_after=-1;assert(ABoxBootV2_StateLoad(&r));assert(r.last_error!=99U);
    memset(&desc,0,sizeof(desc));desc.magic=ABOX_BOOT_V2_DESCRIPTOR_MAGIC;desc.abi_version=ABOX_BOOT_V2_DESCRIPTOR_ABI;desc.length=sizeof(desc);desc.feature_flags=ABOX_BOOT_V2_REQUIRED_FEATURES;desc.crc32=ABoxBootV2_Crc32(&desc,(uint32_t)offsetof(ABoxBootV2Descriptor,crc32));memcpy(flash_mem+ABOX_BOOT_V2_DESCRIPTOR_ADDR-BASE,&desc,sizeof(desc));assert(ABoxBootV2_DescriptorRead(&desc));assert(ABoxBootV2_DescriptorReady(&desc));

    memset(&r,0,sizeof(r));r.state=ABOX_BOOT_V2_INSTALL_PENDING;r.stable_slot=0U;r.candidate_slot=1U;r.stable_image_size=16U;r.stable_image_crc32=0x11111111U;r.image_size=32U;r.image_crc32=0x22222222U;memcpy(r.stable_image_version,"stable",7U);memcpy(r.image_version,"candidate",10U);assert(ABoxBootV2_StateSave(&r));
    install_calls=jump_calls=0U;reset_value=0U;image_valid=1U;install_result=ABOX_BOOT_V2_INSTALL_OK;assert(ABoxBootV2Boot_Init(&boot_port));ABoxBootV2Boot_Task();assert(install_calls==1U&&jump_calls==1U);assert(ABoxBootV2_StateLoad(&r));assert(r.state==ABOX_BOOT_V2_TRIAL&&r.trial_fail_count==0U);
    for(uint8_t attempt=1U;attempt<=3U;++attempt){reset_value=3U;assert(ABoxBootV2Boot_Init(&boot_port));ABoxBootV2Boot_Task();assert(ABoxBootV2_StateLoad(&r));assert(r.trial_fail_count==attempt);}
    assert(r.state==ABOX_BOOT_V2_ROLLBACK);reset_value=0U;assert(ABoxBootV2Boot_Init(&boot_port));ABoxBootV2Boot_Task();assert(install_calls==2U);assert(ABoxBootV2_StateLoad(&r));assert(r.state==ABOX_BOOT_V2_ROLLBACK&&r.rollback_installed&&r.rollback_report_pending);
    /* A lost ACK save must retain the readable rollback record. */
    out=r;write_fail_after=5;write_calls=0;assert(!ABoxBootV2_AcknowledgeRollback());write_fail_after=-1;
    assert(ABoxBootV2_StateLoad(&r));assert(!memcmp(&r,&out,sizeof(r)));
    assert(ABoxBootV2_AcknowledgeRollback());assert(ABoxBootV2_StateLoad(&r));assert(r.state==ABOX_BOOT_V2_CONFIRMED&&!r.rollback_report_pending);
    assert(r.candidate_slot==r.stable_slot&&r.image_size==r.stable_image_size&&r.image_crc32==r.stable_image_crc32);
    assert(!strcmp(r.image_version,r.stable_image_version));
    assert(r.trial_fail_count==3U&&r.last_error==ABOX_BOOT_V2_ERROR_ROLLED_BACK);
    /* Validate the actual installed image after a fresh boot, not a constant
     * "valid" mock which hides stale candidate metadata. */
    match_image_metadata=1U;uint32_t previous_jumps=jump_calls;
    assert(ABoxBootV2Boot_Init(&boot_port));ABoxBootV2Boot_Task();
    assert(jump_calls==previous_jumps+1U&&install_calls==2U);
    r.state=ABOX_BOOT_V2_ROLLBACK;r.rollback_report_pending=1U;r.rollback_installed=1U;
    r.stable_image_size=0U;assert(ABoxBootV2_StateSave(&r));
    assert(!ABoxBootV2_AcknowledgeRollback());assert(ABoxBootV2_StateLoad(&out));
    assert(out.state==ABOX_BOOT_V2_ROLLBACK&&out.rollback_report_pending);
    return 0;
}
