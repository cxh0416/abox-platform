#include "drv_usb_core.h"
#include <assert.h>
#include <stdint.h>

void usb_udelay(const uint32_t usec) { (void)usec; }
void usb_mdelay(const uint32_t msec) { (void)msec; }

int main(void)
{
    usb_gr registers={0};
    usb_core_regs core={0};
    uint32_t fifo=0U;
    uint8_t data[5]={0xa1U,0xb2U,0xc3U,0xd4U,0xe5U};
    core.gr=&registers;core.DFIFO[0]=&fifo;

    /* The last FIFO word must not contain bytes past the requested payload. */
    assert(usb_txfifo_write(&core,data,0U,1U)==USB_OK && fifo==0x000000a1U);
    assert(usb_txfifo_write(&core,data,0U,2U)==USB_OK && fifo==0x0000b2a1U);
    assert(usb_txfifo_write(&core,data,0U,3U)==USB_OK && fifo==0x00c3b2a1U);
    assert(usb_txfifo_write(&core,data,0U,5U)==USB_OK && fifo==0x000000e5U);

    /* A short read must not overwrite the bytes after the destination. */
    fifo=0x44332211U;
    for(unsigned len=1U;len<=3U;++len) {
        uint8_t result[5]={0xffU,0xffU,0xffU,0xffU,0xffU};
        assert(usb_rxfifo_read(&core,result,(uint16_t)len)==result+len);
        assert(result[0]==0x11U && result[len]==0xffU);
        if(len>1U)assert(result[1]==0x22U);
        if(len>2U)assert(result[2]==0x33U);
    }

    /* A stuck peripheral must return instead of spinning forever. */
    assert(usb_txfifo_flush(&core,0U)==USB_FAIL);
    assert(usb_rxfifo_flush(&core)==USB_FAIL);
    {
        usb_core_basic config={0};config.phy_itf=USB_EMBEDDED_PHY;
        assert(usb_core_init(config,&core)==USB_FAIL);
    }
    return 0;
}
