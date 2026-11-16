#ifndef ABOX_DMA_RESOURCE_H
#define ABOX_DMA_RESOURCE_H
/* The owner holds a channel from successful Claim through Release. */
int ABoxDmaResource_Claim(unsigned controller, unsigned channel);
void ABoxDmaResource_Release(unsigned controller, unsigned channel);
#endif
