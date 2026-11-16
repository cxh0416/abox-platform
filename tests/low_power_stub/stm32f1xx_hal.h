#include "common.h"
typedef struct { uint32_t CR,CSR; } FakePwr;
#define PWR ((FakePwr *)fake_power)
#define __HAL_RCC_PWR_CLK_ENABLE() (++fake_clocks)
typedef struct { uint32_t CR,CFGR; } FakeRcc;
#define RCC ((FakeRcc *)&fake_rcc_ctl)
#define RCC_CR_PLLON 0x100U
#define RCC_SYSCLKSOURCE_STATUS_HSI 0U
#define RCC_SYSCLKSOURCE_STATUS_HSE 1U
#define RCC_SYSCLKSOURCE_STATUS_PLLCLK 2U
#define RCC_CFGR_PLLSRC 0x10000U
#define __HAL_RCC_GET_SYSCLK_SOURCE() fake_rcc_source
