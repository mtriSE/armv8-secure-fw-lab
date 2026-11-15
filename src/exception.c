#include "exception.h"
#include "printf.h"
#include "sysreg.h"

#include <stddef.h>

_Static_assert(sizeof(struct trap_frame) == TF_SIZE, "trap frame size");
_Static_assert(offsetof(struct trap_frame, elr) == TF_ELR, "elr offset");
_Static_assert(offsetof(struct trap_frame, spsr) == TF_SPSR, "spsr offset");

static const char *const vec_names[16] = {
    "cur_sp0_sync", "cur_sp0_irq", "cur_sp0_fiq", "cur_sp0_serror",
    "cur_spx_sync", "cur_spx_irq", "cur_spx_fiq", "cur_spx_serror",
    "low64_sync",   "low64_irq",   "low64_fiq",   "low64_serror",
    "low32_sync",   "low32_irq",   "low32_fiq",   "low32_serror",
};

static struct fault_record *expected;

void expect_fault(struct fault_record *rec)
{
    rec->taken = 0;
    expected = rec;
}

static const char *ec_name(uint64_t ec)
{
    switch (ec) {
    case EC_UNKNOWN:  return "unknown/undefined instruction";
    case EC_SVC64:    return "svc (AArch64)";
    case EC_SMC64:    return "smc (AArch64)";
    case EC_IABT_CUR: return "instruction abort, same EL";
    case EC_PC_ALIGN: return "PC alignment fault";
    case EC_DABT_CUR: return "data abort, same EL";
    case EC_SP_ALIGN: return "SP alignment fault";
    case EC_BRK64:    return "brk (AArch64)";
    default:          return "?";
    }
}

static void __attribute__((noreturn)) panic(uint64_t vec, struct trap_frame *tf,
                                            uint64_t esr, uint64_t far)
{
    printf("\n*** EL3 exception: %s ***\n", vec_names[vec]);
    printf("ESR_EL3=0x%lx EC=0x%lx (%s) IL=%lu ISS=0x%lx\n",
           esr, ESR_EC(esr), ec_name(ESR_EC(esr)), ESR_IL(esr), ESR_ISS(esr));
    printf("ELR_EL3=0x%lx FAR_EL3=0x%lx SPSR_EL3=0x%lx\n", tf->elr, far, tf->spsr);
    for (int i = 0; i < 31; i++)
        printf("x%d=0x%lx%s", i, tf->x[i], (i % 4 == 3 || i == 30) ? "\n" : " ");
    printf("System halted.\n");
    for (;;)
        __asm__ volatile("wfe");
}

void el3_exception_handler(uint64_t vec, struct trap_frame *tf)
{
    uint64_t esr = read_sysreg(esr_el3);
    uint64_t far = read_sysreg(far_el3);

    if (vec == VEC_CUR_SPX_SYNC && expected != NULL) {
        expected->esr = esr;
        expected->elr = tf->elr;
        expected->far = far;
        expected->taken = 1;
        expected = NULL;
        tf->elr += 4;   /* skip the faulting instruction (all A64 insns are 4 bytes) */
        return;
    }
    panic(vec, tf, esr, far);
}
