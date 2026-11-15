#ifndef PAYLOAD_H
#define PAYLOAD_H
#include <stdint.h>

/* Loads 8 bytes from addr with a single ldr; returns 0 if that ldr faulted
 * and was skipped by the handler. */
static inline uint64_t load64(uint64_t addr)
{
    uint64_t v;
    __asm__ volatile("mov %0, xzr\n\tldr %0, [%1]" : "=&r"(v) : "r"(addr) : "memory");
    return v;
}

void irq_handler(void);
#endif
