#include <stdint.h>
typedef struct { int unused; } UART_HandleTypeDef;
uint32_t HAL_GetTick(void);
#define ABox_PortGetTickMs HAL_GetTick
