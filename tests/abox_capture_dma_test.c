#include <assert.h>
#include <stdint.h>
#include "abox_capture_dma_backend.h"
#include "abox_dma.h"
static unsigned reserved[6][5], running[6][5], overrun[6][5], dma_owned[3][8], events;
static ABoxCaptureDmaEvent last_event;
static const ABoxDmaEvents *pending[3][8];
ABoxHalStatus ABoxCaptureHw_DmaReserve(unsigned id,unsigned ch)
{ if(reserved[id][ch])return ABOX_HAL_BUSY;reserved[id][ch]=1U;return ABOX_HAL_OK; }
uintptr_t ABoxCaptureHw_DmaAddress(unsigned id,unsigned ch) { return 0x40000000U+id*1024U+ch*4U; }
void ABoxCaptureHw_DmaEnable(unsigned id,unsigned ch) { running[id][ch]=1U; }
void ABoxCaptureHw_DmaPause(unsigned id,unsigned ch) { running[id][ch]=0U; }
int ABoxCaptureHw_DmaOvercapture(unsigned id,unsigned ch) { return overrun[id][ch]!=0U; }
void ABoxCaptureHw_DmaRelease(unsigned id,unsigned ch) { running[id][ch]=reserved[id][ch]=overrun[id][ch]=0U; }
ABoxHalStatus ABoxDma_Open(unsigned ctl,unsigned ch,const ABoxDmaConfig *c)
{ assert(c->direction==ABOX_DMA_PERIPHERAL_TO_MEMORY && c->peripheral_width==2U);if(dma_owned[ctl][ch])return ABOX_HAL_BUSY;dma_owned[ctl][ch]=1U;return ABOX_HAL_OK; }
ABoxHalStatus ABoxDma_Start(unsigned ctl,unsigned ch,uintptr_t src,uintptr_t dst,uint16_t n,const ABoxDmaEvents *e)
{ assert(dma_owned[ctl][ch] && src && dst && n==4U);pending[ctl][ch]=e;return ABOX_HAL_OK; }
ABoxHalStatus ABoxDma_Abort(unsigned ctl,unsigned ch) { assert(dma_owned[ctl][ch]);return ABOX_HAL_OK; }
ABoxHalStatus ABoxDma_Close(unsigned ctl,unsigned ch) { assert(dma_owned[ctl][ch]);dma_owned[ctl][ch]=0U;pending[ctl][ch]=0;return ABOX_HAL_OK; }
static void event(void *context,unsigned id,unsigned ch,ABoxCaptureDmaEvent e)
{ assert(context==(void*)1 && id==1U && ch==1U);++events;last_event=e; }
int main(void)
{
    static const uint8_t routes[5][4]={{2,3,6,4},{5,7,1,7},{6,0,2,3},{1,4,5,0},{5,4,2,1}};
    ABoxCaptureDmaEvents e={(void*)1,event};uint16_t buffer[4];uint8_t lost=99U;
    assert(ABoxCapture_StopDma(0U,0U,&lost)==ABOX_HAL_INVALID);
    for(unsigned id=1;id<=5U;++id)for(unsigned ch=1;ch<=4U;++ch) {
        unsigned ctl=id==5U?2U:1U,route=routes[id-1U][ch-1U];
        if (!route) { assert(ABoxCapture_StartDma(id,ch,buffer,4U,0U,0)==ABOX_HAL_UNSUPPORTED);continue; }
        assert(ABoxCapture_StartDma(id,ch,buffer,4U,0U,0)==ABOX_HAL_OK);
        assert(dma_owned[ctl][route] && running[id][ch]);
        assert(ABoxCapture_StopDma(id,ch,&lost)==ABOX_HAL_OK && !lost && !dma_owned[ctl][route]);
    }
    assert(ABoxCapture_StartDma(1U,1U,buffer,4U,1U,&e)==ABOX_HAL_OK);
    assert(ABoxCapture_StartDma(3U,3U,buffer,4U,1U,0)==ABOX_HAL_BUSY); /* same DMA1 ch2 */
    pending[1][2]->on_event(pending[1][2]->context,1U,2U,ABOX_DMA_HALF);
    assert(events==1U && last_event==ABOX_CAPTURE_DMA_HALF && running[1][1]);
    overrun[1][1]=1U;pending[1][2]->on_event(pending[1][2]->context,1U,2U,ABOX_DMA_COMPLETE);
    assert(events==2U && last_event==ABOX_CAPTURE_DMA_OVERCAPTURE && !running[1][1]);
    assert(ABoxCapture_StopDma(1U,1U,&lost)==ABOX_HAL_OK && lost);
    assert(ABoxCapture_StartDma(1U,1U,buffer,4U,0U,&e)==ABOX_HAL_OK);
    pending[1][2]->on_event(pending[1][2]->context,1U,2U,ABOX_DMA_TRANSFER_ERROR);
    assert(last_event==ABOX_CAPTURE_DMA_ERROR && ABoxCapture_StopDma(1U,1U,&lost)==ABOX_HAL_ERROR);
    assert(!lost && !running[1][1] && !reserved[1][1]);
    return 0;
}
