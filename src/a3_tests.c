#include "a3_tests.h"
#include "exception.h"
#include "printf.h"
#include "sysreg.h"

#define BAD_ADDR 0x0b000000UL   /* hole in the secure memory map (info mtree) */

/* Each trigger returns the address of the faulting instruction, taken with
 * adr from a local label, so the test can compare it with ELR_EL3. */
static uint64_t trigger_brk(void)
{
    uint64_t pc;
    __asm__ volatile("adr %0, 1f\n1: brk #0x42" : "=r"(pc));
    return pc;
}

static uint64_t trigger_udf(void)
{
    uint64_t pc;
    __asm__ volatile("adr %0, 1f\n1: udf #0" : "=r"(pc));
    return pc;
}

static uint64_t trigger_bad_load(void)
{
    uint64_t pc, val;
    __asm__ volatile("adr %0, 1f\n1: ldr %1, [%2]"
                     : "=&r"(pc), "=r"(val) : "r"(BAD_ADDR) : "memory");
    return pc;
}

static uint64_t trigger_unaligned_load(void)
{
    static uint64_t buf[2];
    uint64_t pc, val;
    __asm__ volatile("adr %0, 1f\n1: ldr %1, [%2]"
                     : "=&r"(pc), "=r"(val) : "r"((uintptr_t)buf + 1) : "memory");
    return pc;
}

static int run(const char *name, uint64_t (*trigger)(void), uint64_t want_ec)
{
    struct fault_record rec;
    uint64_t pc;
    int ok;

    expect_fault(&rec);
    pc = trigger();
    ok = rec.taken && ESR_EC(rec.esr) == want_ec && rec.elr == pc;
    printf("A3: %s: taken=%d EC=0x%lx ISS=0x%lx ELR=0x%lx FAR=0x%lx %s\n",
           name, rec.taken, ESR_EC(rec.esr), ESR_ISS(rec.esr), rec.elr, rec.far,
           ok ? "OK" : "BAD");
    return ok;
}

void a3_run_tests(void)
{
    printf("A3: VBAR_EL3=0x%lx SCTLR_EL3=0x%lx\n", read_sysreg(vbar_el3), read_sysreg(sctlr_el3));
    run("brk", trigger_brk, EC_BRK64);
    run("udf", trigger_udf, EC_UNKNOWN);
    run("bad load", trigger_bad_load, EC_DABT_CUR);
    run("unaligned", trigger_unaligned_load, EC_DABT_CUR);
    printf("A3: done\n");
}
