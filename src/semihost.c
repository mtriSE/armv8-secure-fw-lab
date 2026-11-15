/* Arm semihosting: the firmware asks the debugger/emulator for a service by
 * executing HLT #0xF000 with the operation in x0 and a parameter in x1.
 * Used only to stop QEMU when the run is over, so tests finish at once
 * instead of waiting for a timeout. QEMU must run with -semihosting. */
#include "semihost.h"

#define SYS_EXIT                      0x18
#define ADP_STOPPED_APPLICATION_EXIT  0x20026

void semihost_exit(uint32_t status)
{
    /* AArch64 SYS_EXIT takes a pointer to {reason, status}. */
    uint64_t block[2] = { ADP_STOPPED_APPLICATION_EXIT, status };
    register uint64_t x0 __asm__("x0") = SYS_EXIT;
    register uint64_t x1 __asm__("x1") = (uint64_t)block;

    __asm__ volatile("hlt #0xf000" : : "r"(x0), "r"(x1) : "memory");
    for (;;)
        __asm__ volatile("wfe");
}
