#ifndef MMIO_H
#define MMIO_H
#include <stdint.h>


static inline uint32_t mmio_read32(uintptr_t addr) 
{
    /* cast addr to a pointer to volatile 32-bit, then dereference */
    return *((volatile uint32_t*)addr);
}

static inline void mmio_write32(uintptr_t addr, uint32_t val) 
{
    /* cast addr to a pointer to volatile 32-bit, then store val */
    *((volatile uint32_t*)addr) = val;
}

#endif