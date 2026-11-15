#ifndef EL3_H
#define EL3_H
#include <stdint.h>
#include "exception.h"

/* SCR_EL3: how lower ELs behave (Arm ARM, SCR_EL3). */
#define SCR_NS      (1UL << 0)   /* lower ELs are Non-secure */
#define SCR_IRQ     (1UL << 1)   /* route IRQs to EL3 (we keep 0: EL1 handles them) */
#define SCR_FIQ     (1UL << 2)
#define SCR_EA      (1UL << 3)   /* route external aborts/SError to EL3 */
#define SCR_RES1    (3UL << 4)   /* bits [5:4] RES1 in Armv8.0 */
#define SCR_SMD     (1UL << 7)   /* SMC disable (we keep 0) */
#define SCR_RW      (1UL << 10)  /* next lower EL is AArch64 */

/* SPSR value for "return" to EL1h (EL1, using SP_EL1) with D, A, I, F masked. */
#define SPSR_EL1H_DAIF_MASKED  0x3c5UL

/* SCTLR_EL1 with MMU, caches and alignment checks off; RES1 bits (Armv8.0)
 * set. EL1 system registers are NOT banked between worlds, so EL3 must put
 * them in a known state before every world switch. */
#define SCTLR_EL1_RESET        0x30d00800UL

void el3_eret(uint64_t entry, uint64_t spsr, uint64_t arg) __attribute__((noreturn));
void el3_start_payload(void) __attribute__((noreturn));
void el3_smc_handler(struct trap_frame *tf);
#endif
