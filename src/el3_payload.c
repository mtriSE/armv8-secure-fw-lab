/* A4: EL3 as a tiny "BL31": load the EL1 payload, enter it first in the
 * Secure world, then in the Non-secure world, and serve its SMCs. */
#include "el3.h"
#include "lib.h"
#include "printf.h"
#include "semihost.h"
#include "smccc.h"
#include "sysreg.h"

#include <stddef.h>

/* The payload binary is embedded in flash by src/payload_blob.S; it is
 * linked to run at the NS RAM base reserved in linker/link.ld (__ns_start). */
extern const char payload_blob_start[], payload_blob_end[];
extern char __ns_start[];

/* Copy the payload image to RAM (like BL2 loading BL33). The I-cache must
 * not hold stale instructions for the new code. */
static void load_payload(void)
{
    size_t size = (size_t)(payload_blob_end - payload_blob_start);

    memcpy(__ns_start, payload_blob_start, size);
    __asm__ volatile("dsb sy\n\tic iallu\n\tdsb sy\n\tisb" ::: "memory");
}

/* Put the EL1 system registers in a known state before entering a world. */
static void el1_sysregs_reset(void)
{
    write_sysreg(SCTLR_EL1_RESET, sctlr_el1);
    write_sysreg(0, vbar_el1);
    isb();
}

static uint64_t scr_for(int world)
{
    /* SIF (Secure Instruction Fetch from NS memory) stays 0 on purpose: the
     * Secure EL1 run executes the payload from NS RAM. Real firmware sets
     * SIF=1 and keeps Secure code in Secure memory. */
    uint64_t scr = SCR_RES1 | SCR_RW;
    if (world == WORLD_NONSECURE)
        scr |= SCR_NS;
    return scr;
}

void el3_start_payload(void)
{
    load_payload();
    el1_sysregs_reset();
    write_sysreg(scr_for(WORLD_SECURE), scr_el3);
    isb();
    printf("A4: EL3: entering Secure EL1 payload at %p\n", (void *)__ns_start);
    el3_eret((uint64_t)__ns_start, SPSR_EL1H_DAIF_MASKED, 0);
}

/* SIP_DONE: the payload finished in the current world. After the Secure run,
 * re-enter the payload in the Non-secure world by rewriting the trap frame
 * that eret will consume; after the Non-secure run, stop QEMU. */
static void payload_done(struct trap_frame *tf)
{
    uint64_t failures = tf->x[1];

    if ((read_sysreg(scr_el3) & SCR_NS) == 0) {
        printf("A4: EL3: Secure EL1 done (failures=%lu), entering Non-secure EL1\n",
               failures);
        load_payload();             /* fresh .data for the second run */
        el1_sysregs_reset();
        for (int i = 0; i < 31; i++)
            tf->x[i] = 0;           /* no Secure register values leak to NS */
        tf->elr = (uint64_t)__ns_start;
        tf->spsr = SPSR_EL1H_DAIF_MASKED;
        write_sysreg(scr_for(WORLD_NONSECURE), scr_el3);
        isb();
        return;
    }
    printf("EL3: Non-secure EL1 done (failures=%lu)\n", failures);
    semihost_exit((uint32_t)failures);
}

void el3_smc_handler(struct trap_frame *tf)
{
    /* SMCCC: the function ID is w0 (upper 32 bits ignored). The return
     * address in ELR_EL3 is already the instruction after smc. */
    uint32_t fid = (uint32_t)tf->x[0];

    switch (fid) {
    case SMCCC_VERSION:
        tf->x[0] = SMCCC_VERSION_1_1;
        break;
    case SIP_ADD:
        tf->x[0] = tf->x[1] + tf->x[2] + tf->x[3];
        break;
    case SIP_GET_WORLD:
        tf->x[0] = (read_sysreg(scr_el3) & SCR_NS) ? WORLD_NONSECURE : WORLD_SECURE;
        break;
    case SIP_DONE:
        payload_done(tf);
        break;
    default:
        tf->x[0] = SMCCC_UNKNOWN;
        break;
    }
}
