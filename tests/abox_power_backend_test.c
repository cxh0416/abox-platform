#include "abox_backup_backend.h"
#include "abox_pvd_backend.h"
#include <assert.h>
unsigned pwr_clocks,bkp_clocks;
#ifdef TEST_STM32
#include "stm32f1xx_hal.h"
BKP_TypeDef test_bkp;PWR_TypeDef test_pwr;EXTI_TypeDef test_exti;
#define POWER test_pwr.CR
#define STATUS test_pwr.CSR
#define IMR test_exti.IMR
#define EMR test_exti.EMR
#define RISING test_exti.RTSR
#define FALLING test_exti.FTSR
#define PENDING test_exti.PR
#define BACKUP ((uint32_t *)&test_bkp)
#else
#include "gd32f10x.h"
#include "gd32f10x_pmu.h"
#include "gd32f10x_exti.h"
uint32_t test_bkp_words[48],test_pmu_words[2],test_exti_words[6];
void rcu_periph_clock_enable(uint32_t id) { if(id==RCU_PMU)++pwr_clocks;else {assert(id==RCU_BKPI);++bkp_clocks;} }
#define POWER PMU_CTL
#define STATUS PMU_CS
#define IMR EXTI_INTEN
#define EMR EXTI_EVEN
#define RISING EXTI_RTEN
#define FALLING EXTI_FTEN
#define PENDING EXTI_PD
#define BACKUP test_bkp_words
#endif
int main(void)
{
    uint32_t old;ABoxPvdConfig c={ABOX_PVD_2V7,ABOX_PVD_BOTH,1U,1U};
    POWER=0U;old=ABoxBackupHw_AccessBegin();assert(!old && (POWER&(1U<<8)) && ABoxBackupHw_AccessReady());
    ABoxBackupHw_Write(1U,0x1234U);ABoxBackupHw_Write(10U,0x2345U);
    ABoxBackupHw_Write(11U,0x3456U);ABoxBackupHw_Write(42U,0x4567U);
    assert(BACKUP[1]==0x1234U && BACKUP[10]==0x2345U && BACKUP[16]==0x3456U && BACKUP[47]==0x4567U);
    assert(ABoxBackupHw_Read(42U)==0x4567U);ABoxBackupHw_AccessEnd(old);assert(!(POWER&(1U<<8)));
    ABoxBackupHw_TamperConfigure(1U,0U,1U);
    assert(BACKUP[12]==3U && BACKUP[13]==4U && ABoxBackupHw_TamperConfig()==7U);
    BACKUP[13]=0x304U;assert(ABoxBackupHw_TamperFlags()==3U);
    ABoxBackupHw_TamperClear(1U);assert(BACKUP[13]==5U);
    ABoxBackupHw_TamperConfigure(0U,1U,0U);assert(BACKUP[12]==0U && BACKUP[13]==0U);
    POWER=1U<<8;old=ABoxBackupHw_AccessBegin();ABoxBackupHw_AccessEnd(old);assert(POWER&(1U<<8));
    IMR=EMR=RISING=FALLING=2U;ABoxPvdHw_EnableClock();assert(!ABoxPvdHw_IsEnabled());
    ABoxPvdHw_Configure(&c);
    assert((POWER&(1U<<8)) && (POWER&(1U<<4)) && ((POWER>>5U)&7U)==5U);
    assert(IMR==((1U<<16)|2U) && EMR==IMR && RISING==IMR && FALLING==IMR && PENDING==(1U<<16));
    STATUS=1U<<2;assert(ABoxPvdHw_Read() && ABoxPvdHw_Pending());
    ABoxPvdHw_Disable();assert(!(POWER&(1U<<4)) && (POWER&(1U<<8)) && IMR==2U && EMR==2U && RISING==2U && FALLING==2U);
    assert(pwr_clocks==3U && bkp_clocks==2U);
    return 0;
}
