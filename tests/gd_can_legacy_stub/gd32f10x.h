#ifndef TEST_GD_CAN_LEGACY_H
#define TEST_GD_CAN_LEGACY_H
#include <stdint.h>
extern uint32_t test_can_memory[512];
#define CAN_BASE ((uintptr_t)test_can_memory)
#endif
