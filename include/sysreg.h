#ifndef SYSREG_H
#define SYSREG_H
#include <stdint.h>

/* System registers are not memory: they are read/written with mrs/msr,
 * and the register name must be part of the instruction (a string literal). */
#define read_sysreg(reg) ({                                  \
    uint64_t __v;                                            \
    __asm__ volatile("mrs %0, " #reg : "=r"(__v));           \
    __v;                                                     \
})

#define write_sysreg(val, reg) do {                          \
    uint64_t __v = (uint64_t)(val);                          \
    __asm__ volatile("msr " #reg ", %0" : : "r"(__v));       \
} while (0)

#define isb() __asm__ volatile("isb" ::: "memory")

#endif
