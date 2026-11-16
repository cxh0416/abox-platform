#include "abox_can_port.h"
#include "gd32f10x.h"

static int prepare(void *context,unsigned bus,ABoxCanMode mode)
{
    uint32_t port,standby;(void)context;
    if(bus>1U)return 0;
    if(mode!=ABOX_CAN_NORMAL && mode!=ABOX_CAN_SILENT)return 1;
    port=bus ? GPIOB : GPIOA;standby=1U<<(bus ? 14U : 15U);
    rcu_periph_clock_enable(bus ? RCU_GPIOB : RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    gpio_bit_reset(GPIOB,standby);
    gpio_init(GPIOB,GPIO_MODE_OUT_PP,GPIO_OSPEED_2MHZ,standby);
    rcu_periph_clock_enable(RCU_AF);
    gpio_pin_remap_config(bus ? GPIO_CAN1_REMAP : GPIO_CAN0_FULL_REMAP,DISABLE);
    gpio_init(port,GPIO_MODE_IN_FLOATING,GPIO_OSPEED_2MHZ,1U<<(bus ? 12U : 11U));
    gpio_init(port,GPIO_MODE_AF_PP,GPIO_OSPEED_50MHZ,1U<<(bus ? 13U : 12U));
    return 1;
}
static void release(void *context,unsigned bus,ABoxCanMode mode)
{
    (void)context;
    if(bus<2U && (mode==ABOX_CAN_NORMAL || mode==ABOX_CAN_SILENT))
        gpio_bit_set(GPIOB,1U<<(bus ? 14U : 15U));
}
int ABoxGdV1Can_Bind(void)
{
    static const ABoxCanBoardOps ops={0,prepare,release};
    return ABoxCanPort_BindBoard(&ops);
}
