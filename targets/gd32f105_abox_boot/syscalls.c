#include <errno.h>
#include <stddef.h>
#include <stdint.h>

extern char _heap_start, _heap_end;
void *_sbrk(ptrdiff_t amount)
{
    static uintptr_t current;
    uintptr_t start = (uintptr_t)&_heap_start, end = (uintptr_t)&_heap_end, next;
    if (!current) current = start;
    if ((amount >= 0 && (uintptr_t)amount > end - current) ||
        (amount < 0 && (uintptr_t)(-(amount + 1)) + 1U > current - start)) {
        errno = ENOMEM; return (void *)-1;
    }
    next = amount >= 0 ? current + (uintptr_t)amount : current - ((uintptr_t)(-(amount + 1)) + 1U);
    uintptr_t old = current; current = next; return (void *)old;
}
