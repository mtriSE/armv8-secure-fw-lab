#ifndef EXCEPTION_H
#define EXCEPTION_H

/* Shared by vectors.S and C: keep the layout in one place. */
#define TF_X30   (30 * 8)   /* x0..x29 in pairs at 0..232, x30 at 240 */
#define TF_ELR   (31 * 8)   /* 248 */
#define TF_SPSR  (32 * 8)   /* 256 */
#define TF_SIZE  (34 * 8)   /* 272: one padding slot keeps sp 16-byte aligned */

/* Which of the 16 vector entries was taken (passed in x0). */
#define VEC_CUR_SP0_SYNC    0
#define VEC_CUR_SP0_IRQ     1
#define VEC_CUR_SP0_FIQ     2
#define VEC_CUR_SP0_SERROR  3
#define VEC_CUR_SPX_SYNC    4
#define VEC_CUR_SPX_IRQ     5
#define VEC_CUR_SPX_FIQ     6
#define VEC_CUR_SPX_SERROR  7
#define VEC_LOW64_SYNC      8
#define VEC_LOW64_IRQ       9
#define VEC_LOW64_FIQ       10
#define VEC_LOW64_SERROR    11
#define VEC_LOW32_SYNC      12
#define VEC_LOW32_IRQ       13
#define VEC_LOW32_FIQ       14
#define VEC_LOW32_SERROR    15

#ifndef __ASSEMBLER__
#include <stdint.h>

struct trap_frame {
    uint64_t x[31];     /* x0..x30 */
    uint64_t elr;       /* ELR_EL3: where eret will return */
    uint64_t spsr;      /* SPSR_EL3: PSTATE to restore */
    uint64_t pad;
};

/* ESR_ELx fields */
#define ESR_EC(esr)   (((esr) >> 26) & 0x3f)
#define ESR_IL(esr)   (((esr) >> 25) & 0x1)
#define ESR_ISS(esr)  ((esr) & 0x1ffffff)

#define EC_UNKNOWN     0x00
#define EC_SVC64       0x15
#define EC_SMC64       0x17
#define EC_IABT_CUR    0x21
#define EC_PC_ALIGN    0x22
#define EC_DABT_CUR    0x25
#define EC_SP_ALIGN    0x26

/* Data/instruction abort ISS: fault status code and write-not-read */
#define ISS_DFSC(iss)  ((iss) & 0x3f)
#define ISS_WNR(iss)   (((iss) >> 6) & 1)
#define EC_BRK64       0x3c

void el3_exception_handler(uint64_t vec, struct trap_frame *tf);
void el1_exception_handler(uint64_t vec, struct trap_frame *tf);

/* Fault self-test support: arm before a deliberate fault. The handler records
 * ESR/ELR/FAR, skips the faulting instruction and returns instead of halting.
 * expect_fault(NULL) disarms (when the access is expected NOT to fault). */
struct fault_record {
    uint64_t esr, elr, far;
    int taken;
};
void expect_fault(struct fault_record *rec);

#endif /* __ASSEMBLER__ */
#endif
