/* SMC Calling Convention (Arm DEN 0028): function ID in w0, arguments in
 * x1..x6, results in x0..x3. Shared by the EL3 dispatcher and the payload. */
#ifndef SMCCC_H
#define SMCCC_H

#include <stdint.h>

/* Function ID layout: bit 31 = fast call, bit 30 = SMC64,
 * bits [29:24] = owning entity (0 = Arm architecture, 2 = SiP), [15:0] = number. */
#define SMCCC_FAST_CALL     (1u << 31)
#define SMCCC_SMC64         (1u << 30)
#define SMCCC_OEN_SIP       (2u << 24)

#define SMCCC_VERSION       0x80000000u   /* Arm arch call, SMC32 */
#define SMCCC_VERSION_1_1   0x10001u
#define SMCCC_UNKNOWN       ((uint64_t)-1)  /* "not supported" */

/* Our Silicon-Provider (SiP) services */
#define SIP_FID(n)          (SMCCC_FAST_CALL | SMCCC_SMC64 | SMCCC_OEN_SIP | (n))
#define SIP_ADD             SIP_FID(1)    /* x0 = x1 + x2 + x3 */
#define SIP_GET_WORLD       SIP_FID(2)    /* x0 = WORLD_SECURE / WORLD_NONSECURE */
#define SIP_DONE            SIP_FID(3)    /* x1 = failure count; does not return */

#define WORLD_SECURE        0
#define WORLD_NONSECURE     1

#ifndef __ASSEMBLER__
/* Issue an SMC. EL3 restores every register from its trap frame, so only
 * x0 (the result) changes. */
static inline uint64_t smc_call(uint64_t fid, uint64_t a1, uint64_t a2, uint64_t a3)
{
    register uint64_t x0 __asm__("x0") = fid;
    register uint64_t x1 __asm__("x1") = a1;
    register uint64_t x2 __asm__("x2") = a2;
    register uint64_t x3 __asm__("x3") = a3;

    __asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) : : "memory");
    return x0;
}
#endif

#endif
