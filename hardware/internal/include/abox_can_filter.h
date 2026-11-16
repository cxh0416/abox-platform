#ifndef ABOX_CAN_FILTER_H
#define ABOX_CAN_FILTER_H
#include "abox_can_port.h"

/* Encode a semantic filter into the common F105 32-bit ID/mask layout. */
static inline int ABoxCanFilter_Encode(const ABoxCanFilter *f, uint32_t *id, uint32_t *mask)
{
    uint32_t limit, shift;
    if (!f || !id || !mask || f->enabled > 1U) return 0;
    *id = *mask = 0U;
    if (!f->enabled) return 1;
    if (f->extended > 1U || f->match_remote > 1U || f->remote > 1U) return 0;
    limit = f->extended ? 0x1FFFFFFFU : 0x7FFU;
    if (f->id > limit || f->mask > limit) return 0;
    shift = f->extended ? 3U : 21U;
    *id = (f->id << shift) | (f->extended ? 4U : 0U) | (f->remote ? 2U : 0U);
    *mask = (f->mask << shift) | 4U | (f->match_remote ? 2U : 0U);
    return 1;
}
#endif
