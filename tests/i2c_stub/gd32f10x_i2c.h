#include "gd32f10x.h"
extern uint32_t fake_i2c_rise[2];
#define I2C_RT(id) (fake_i2c_rise[(id)-1U])
