/* EL1 exception handler of the payload: same design as the EL3 one (A3),
 * but reads the EL1 syndrome registers. */
#include "exception.h"
#include "payload.h"
#include "printf.h"
#include "sysreg.h"

#include <stddef.h>

_Static_assert(sizeof(struct trap_frame) == TF_SIZE, "trap frame size");

static struct fault_record *expected;

void expect_fault(struct fault_record *rec)
{
    if (rec != NULL)
        rec->taken = 0;
    expected = rec;     /* NULL disarms */
}

static void __attribute__((noreturn)) panic(uint64_t vec, struct trap_frame *tf,
                                            uint64_t esr, uint64_t far)
{
    printf("\n*** EL1 exception: vector %lu ***\n", vec);
    printf("ESR_EL1=0x%lx EC=0x%lx ISS=0x%lx\n", esr, ESR_EC(esr), ESR_ISS(esr));
    printf("ELR_EL1=0x%lx FAR_EL1=0x%lx SPSR_EL1=0x%lx\n", tf->elr, far, tf->spsr);
    for (int i = 0; i < 31; i++)
        printf("x%d=0x%lx%s", i, tf->x[i], (i % 4 == 3 || i == 30) ? "\n" : " ");
    printf("System halted.\n");
    for (;;)
        __asm__ volatile("wfe");
}

void el1_exception_handler(uint64_t vec, struct trap_frame *tf)
{
    if (vec == VEC_CUR_SPX_IRQ) {
        irq_handler();
        return;
    }

    uint64_t esr = read_sysreg(esr_el1);
    uint64_t far = read_sysreg(far_el1);

    if (vec == VEC_CUR_SPX_SYNC && expected != NULL) {
        expected->esr = esr;
        expected->elr = tf->elr;
        expected->far = far;
        expected->taken = 1;
        expected = NULL;
        tf->elr += 4;   /* skip the faulting instruction */
        return;
    }
    panic(vec, tf, esr, far);
}
