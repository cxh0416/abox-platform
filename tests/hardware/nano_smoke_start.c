#include <stdint.h>

extern uint32_t _estack, _sbss, _ebss;
extern int main(void);
void Reset_Handler(void);

__attribute__((section(".isr_vector"), used))
const uintptr_t nano_vectors[] = {(uintptr_t)&_estack, (uintptr_t)Reset_Handler};

/* A debugger or Cortex-M emulator reads the exit status in R0 at BKPT. */
__attribute__((noreturn))
static void finish(int status)
{
    __asm volatile("mov r0, %0\n bkpt #0\n b ." : : "r"(status) : "r0", "memory");
    __builtin_unreachable();
}

void __wrap___assert_func(const char *file, int line, const char *function, const char *expression)
{
    (void)file;
    (void)function;
    (void)expression;
    finish(line);
}

void Reset_Handler(void)
{
    __asm volatile("cpsid i" : : : "memory");
    for (uint32_t *p = &_sbss; p < &_ebss; ++p) *p = 0;
    finish(main());
}
