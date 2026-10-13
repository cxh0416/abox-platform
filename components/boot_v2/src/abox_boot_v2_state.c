#include "abox_boot_v2.h"
#include "abox_platform_port.h"
#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(ABoxBootV2Record) == 112U, "Boot State V3 ABI changed");
static ABoxBootV2Layout g_layout;
static uint8_t g_bound;
void ABoxBootV2_ImageBindLayoutInternal(const ABoxBootV2Layout *layout);

uint32_t ABoxBootV2_Crc32(const void *data, uint32_t length)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFU;
    while (length--) { uint8_t bit; crc ^= *p++; for (bit = 0U; bit < 8U; ++bit) crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U))); }
    return crc ^ 0xFFFFFFFFU;
}

int ABoxBootV2_StateBindLayout(const ABoxBootV2Layout *layout)
{
    if (!layout || !layout->state_a_addr || !layout->state_b_addr || layout->state_a_addr == layout->state_b_addr || layout->page_size < sizeof(ABoxBootV2Record)) return 0;
    g_layout = *layout; ABoxBootV2_ImageBindLayoutInternal(layout); g_bound = 1U; return 1;
}
static int read_record(uint32_t address, ABoxBootV2Record *record) { return ABox_PortFlashRead(address, (uint8_t *)record, sizeof(*record)); }
static int valid(const ABoxBootV2Record *r) { return r && r->magic == ABOX_BOOT_V2_STATE_MAGIC && r->version == ABOX_BOOT_V2_STATE_VERSION && r->length == sizeof(*r) && r->record_crc32 == ABoxBootV2_Crc32(r, (uint32_t)offsetof(ABoxBootV2Record, record_crc32)); }

int ABoxBootV2_StateLoad(ABoxBootV2Record *record)
{
    ABoxBootV2Record a, b; int av, bv;
    if (!g_bound || !record || !read_record(g_layout.state_a_addr, &a) || !read_record(g_layout.state_b_addr, &b)) return 0;
    av = valid(&a); bv = valid(&b); if (!av && !bv) { memset(record, 0, sizeof(*record)); return 0; }
    *record = (!bv || (av && a.sequence >= b.sequence)) ? a : b; return 1;
}
int ABoxBootV2_StateDiagnostics(ABoxBootV2Diagnostics *d)
{
    ABoxBootV2Record a, b; int av, bv;
    if (!g_bound || !d || !read_record(g_layout.state_a_addr, &a) || !read_record(g_layout.state_b_addr, &b)) return 0;
    av=valid(&a); bv=valid(&b); memset(d,0,sizeof(*d)); d->page_a_valid=(uint8_t)av; d->page_b_valid=(uint8_t)bv; d->page_a_sequence=a.sequence; d->page_b_sequence=b.sequence; d->selected_page=0xFFU; d->selected_valid=(uint8_t)(av||bv);
    if(av&&(!bv||a.sequence>=b.sequence)){d->selected_page=0U;d->selected_sequence=a.sequence;}else if(bv){d->selected_page=1U;d->selected_sequence=b.sequence;} return d->selected_valid;
}
int ABoxBootV2_StateSave(const ABoxBootV2Record *input)
{
    ABoxBootV2Record old,a,b,next,verify; uint32_t address,off;
    if(!g_bound||!input||!read_record(g_layout.state_a_addr,&a)||!read_record(g_layout.state_b_addr,&b))return 0;
    memset(&old,0,sizeof(old)); (void)ABoxBootV2_StateLoad(&old); next=*input; next.magic=ABOX_BOOT_V2_STATE_MAGIC; next.version=ABOX_BOOT_V2_STATE_VERSION; next.length=sizeof(next); next.sequence=valid(&old)?old.sequence+1U:1U; next.record_crc32=ABoxBootV2_Crc32(&next,(uint32_t)offsetof(ABoxBootV2Record,record_crc32));
    address=!valid(&a)?g_layout.state_a_addr:!valid(&b)?g_layout.state_b_addr:(a.sequence<=b.sequence?g_layout.state_a_addr:g_layout.state_b_addr);
    if (!ABox_PortFlashBegin()) return 0;
    if (!ABox_PortFlashErasePage(address)) { ABox_PortFlashEnd(); return 0; }
    for(off=0U;off<sizeof(next);off+=4U)if(!ABox_PortFlashWrite(address+off,((const uint8_t*)&next)+off,4U)){ABox_PortFlashEnd();return 0;}
    ABox_PortFlashEnd(); return read_record(address,&verify)&&memcmp(&verify,&next,sizeof(next))==0&&valid(&verify);
}
int ABoxBootV2_Confirm(void){ABoxBootV2Record s;if(!ABoxBootV2_StateLoad(&s)||s.state!=ABOX_BOOT_V2_TRIAL)return 1;s.state=ABOX_BOOT_V2_CONFIRMED;s.stable_slot=s.candidate_slot;s.stable_image_size=s.image_size;s.stable_image_crc32=s.image_crc32;memcpy(s.stable_image_version,s.image_version,sizeof(s.stable_image_version));s.trial_fail_count=0U;return ABoxBootV2_StateSave(&s);}
int ABoxBootV2_AcknowledgeRollback(void)
{
    ABoxBootV2Record s;
    if (!ABoxBootV2_StateLoad(&s) || !s.rollback_report_pending) return 1;
    if (s.state != ABOX_BOOT_V2_ROLLBACK || !s.rollback_installed ||
        s.stable_slot > 1U || s.stable_image_size == 0U) return 0;
    /* The next boot validates image_* against the installed stable App. */
    s.candidate_slot = s.stable_slot;
    s.image_size = s.stable_image_size;
    s.image_crc32 = s.stable_image_crc32;
    memcpy(s.image_version, s.stable_image_version, sizeof(s.image_version));
    s.rollback_report_pending = 0U;
    s.rollback_installed = 0U;
    s.state = ABOX_BOOT_V2_CONFIRMED;
    return ABoxBootV2_StateSave(&s);
}
const char *ABoxBootV2_StateName(uint32_t s){static const char*const n[]={"NORMAL","INSTALL_PENDING","INSTALLING","TRIAL","CONFIRMED","ROLLBACK"};return s<6U?n[s]:"UNKNOWN";}
