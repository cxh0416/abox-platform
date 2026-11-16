#ifndef ABOX_USB_OTG_PORT_H
#define ABOX_USB_OTG_PORT_H
#include <stdint.h>
/* F105 HPRT: never write one to enable/disable or change flags. */
static inline uint32_t ABoxUsbOtgPort_Signal(uint32_t value,uint8_t suspend,uint8_t resume)
{
    value &= ~((1UL<<1)|(1UL<<2)|(1UL<<3)|(1UL<<5)|(1UL<<6));
    if(suspend)value|=1UL<<7;else if(!resume)value&=~(1UL<<7);
    if(resume)value|=1UL<<6;
    return value;
}
#endif
