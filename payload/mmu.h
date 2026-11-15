#ifndef MMU_H
#define MMU_H
#include <stdint.h>
#define UNMAPPED_VA 0x80000000UL
void mmu_enable(void);
/* VA of the 0xC000_0000 alias for an address in the payload RAM block. */
uint64_t mmu_alias(uint64_t ram_va);
#endif
