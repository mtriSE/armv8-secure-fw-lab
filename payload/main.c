/* EL1 payload. EL3 runs it twice: first in the Secure world, then in the
 * Non-secure world. It asks EL3 which world it is in with an SMC, exercises
 * the SMC interface, and probes Secure RAM to see TrustZone at work. */
#include "exception.h"
#include "payload.h"
#include "platform.h"
#include "printf.h"
#include "smccc.h"
#include "sysreg.h"

#include <stddef.h>

static int failures;

const char *payload_ok(int ok)
{
    if (!ok)
        failures++;
    return ok ? "OK" : "BAD";
}

/* EL3 .data starts at the Secure RAM base, and its first variable is
 * data_probe = 0x1234 (A1). Secure EL1 may read it; Non-secure EL1 may not:
 * the access never reaches RAM and comes back as an external abort. */
static void probe_secure_ram(const char *w, int world)
{
    struct fault_record rec;
    uint64_t v;

    expect_fault(&rec);
    v = load64(SECURE_RAM_BASE);
    expect_fault(NULL);

    if (world == WORLD_SECURE) {
        printf("A4: [%s] read secure RAM %p: val=0x%lx %s\n", w,
               (void *)SECURE_RAM_BASE, v, payload_ok(!rec.taken && v == 0x1234));
    } else {
        uint64_t iss = ESR_ISS(rec.esr);
        printf("A4: [%s] read secure RAM %p: fault EC=0x%lx DFSC=0x%lx FAR=0x%lx %s\n",
               w, (void *)SECURE_RAM_BASE, ESR_EC(rec.esr), ISS_DFSC(iss), rec.far,
               payload_ok(rec.taken && ESR_EC(rec.esr) == EC_DABT_CUR &&
                      rec.far == SECURE_RAM_BASE));
    }
}

void payload_main(void)
{
    uint64_t world = smc_call(SIP_GET_WORLD, 0, 0, 0);
    const char *w = world == WORLD_SECURE ? "S" : "NS";
    uint64_t r;

    printf("A4: [%s] EL1 payload: CurrentEL=%lu\n", w, (read_sysreg(CurrentEL) >> 2) & 3);
    printf("A4: [%s] smc GET_WORLD=%lu (%s)\n", w, world,
           world == WORLD_SECURE ? "secure" : "non-secure");

    r = smc_call(SMCCC_VERSION, 0, 0, 0);
    printf("A4: [%s] smc SMCCC_VERSION=0x%lx %s\n", w, r, payload_ok(r == SMCCC_VERSION_1_1));
    r = smc_call(SIP_ADD, 1, 2, 3);
    printf("A4: [%s] smc ADD(1,2,3)=%lu %s\n", w, r, payload_ok(r == 6));
    r = smc_call(SIP_FID(0x77), 0, 0, 0);
    printf("A4: [%s] smc unknown=0x%lx %s\n", w, r, payload_ok(r == SMCCC_UNKNOWN));

    probe_secure_ram(w, (int)world);

    /* The rest only runs in the Normal world, like a rich OS would. */
    if (world == WORLD_NONSECURE)
        a5_run();

    smc_call(SIP_DONE, (uint64_t)failures, 0, 0);   /* does not return */
}

/* No interrupts are enabled yet (A6). */
void irq_handler(void)
{
    printf("A4: unexpected IRQ\n");
    for (;;)
        __asm__ volatile("wfe");
}
