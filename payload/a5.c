/* A5 checks, run in Non-secure EL1. */
#include "exception.h"
#include "mmu.h"
#include "payload.h"
#include "printf.h"
#include "sysreg.h"

#include <stddef.h>

static uint8_t buf[16] __attribute__((aligned(16)));
static volatile uint64_t marker;

/* Unaligned 8-byte load from buf+1; returns 1 if it faulted. */
static int unaligned_load(uint64_t *val, uint64_t *esr)
{
    struct fault_record rec;

    expect_fault(&rec);
    *val = load64((uint64_t)buf + 1);
    expect_fault(NULL);
    *esr = rec.esr;
    return rec.taken;
}

void a5_run(void)
{
    uint64_t v, esr;
    int faulted;

    for (int i = 0; i < 16; i++)
        buf[i] = (uint8_t)i;

    /* MMU off: every data access is Device memory -> unaligned faults. */
    faulted = unaligned_load(&v, &esr);
    printf("A5: before MMU: unaligned load fault EC=0x%lx DFSC=0x%lx %s\n",
           ESR_EC(esr), ISS_DFSC(ESR_ISS(esr)),
           payload_ok(faulted && ESR_EC(esr) == EC_DABT_CUR &&
                      ISS_DFSC(ESR_ISS(esr)) == 0x21));

    mmu_enable();
    printf("A5: MMU on: SCTLR_EL1=0x%lx TCR_EL1=0x%lx MAIR_EL1=0x%lx\n",
           read_sysreg(sctlr_el1), read_sysreg(tcr_el1), read_sysreg(mair_el1));

    /* RAM is Normal memory now: the same load is allowed (SCTLR_EL1.A = 0). */
    faulted = unaligned_load(&v, &esr);
    printf("A5: after MMU: unaligned load val=0x%lx %s\n", v,
           payload_ok(!faulted && v == 0x0807060504030201UL));

    /* Two VAs, one PA: write through one, read through the other. */
    marker = 0x5a5a;
    v = *(volatile uint64_t *)mmu_alias((uint64_t)&marker);
    printf("A5: alias %p -> %p reads 0x%lx %s\n", (void *)mmu_alias((uint64_t)&marker),
           (void *)&marker, v, payload_ok(v == 0x5a5a));

    /* No descriptor for this VA: translation fault at level 1 (DFSC 0x05). */
    {
        struct fault_record rec;

        expect_fault(&rec);
        (void)load64(UNMAPPED_VA);
        expect_fault(NULL);
        printf("A5: unmapped %p: fault EC=0x%lx DFSC=0x%lx FAR=0x%lx %s\n",
               (void *)UNMAPPED_VA, ESR_EC(rec.esr), ISS_DFSC(ESR_ISS(rec.esr)), rec.far,
               payload_ok(rec.taken && ISS_DFSC(ESR_ISS(rec.esr)) == 0x05 &&
                          rec.far == UNMAPPED_VA));
    }
    printf("A5: done\n");
}
