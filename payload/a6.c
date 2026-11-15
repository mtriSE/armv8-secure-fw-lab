/* A6: periodic Non-secure EL1 physical timer interrupts through GICv3. */
#include "gic.h"
#include "gic_ns.h"
#include "payload.h"
#include "printf.h"
#include "sysreg.h"

#define TICKS       5
#define TIMER_HZ    100     /* 10 ms period */

#define CNTP_CTL_ENABLE  (1UL << 0)

static volatile unsigned int ticks;
static uint64_t period;     /* in counter cycles */

static uint64_t counter(void)
{
    isb();      /* CNTPCT can be read speculatively, early */
    return read_sysreg(cntpct_el0);
}

/* Called from the EL1 IRQ vector (payload/exception.c). */
void irq_handler(void)
{
    uint32_t intid = gic_ack();

    if (intid == INTID_SPURIOUS)
        return;                     /* nothing to acknowledge */

    if (intid == INTID_NS_EL1_PTIMER) {
        ticks++;
        printf("A6: tick %u intid=%u\n", ticks, intid);
        /* The timer output is level-sensitive: it stays asserted until the
         * timer is reprogrammed or stopped, so do that before EOI or the
         * same interrupt fires again at once. */
        if (ticks < TICKS)
            write_sysreg(period, cntp_tval_el0);
        else
            write_sysreg(0, cntp_ctl_el0);
    } else {
        printf("A6: unexpected intid=%u\n", intid);
    }
    gic_eoi(intid);
}

void a6_run(void)
{
    uint64_t freq = read_sysreg(cntfrq_el0);
    uint64_t t0, t1;

    printf("A6: CNTFRQ_EL0=%lu\n", freq);
    period = freq / TIMER_HZ;

    gic_cpu_init();
    gic_enable_ppi(INTID_NS_EL1_PTIMER, 0x80);

    t0 = counter();
    write_sysreg(period, cntp_tval_el0);       /* fire in 10 ms */
    write_sysreg(CNTP_CTL_ENABLE, cntp_ctl_el0);
    isb();

    __asm__ volatile("msr daifclr, #2" ::: "memory");   /* unmask IRQ (PSTATE.I) */
    while (ticks < TICKS)
        __asm__ volatile("wfi");
    __asm__ volatile("msr daifset, #2" ::: "memory");

    t1 = counter();
    /* The timer cannot fire early, so 5 periods is a hard lower bound. */
    printf("A6: %u timer IRQs in %lu ms %s\n", ticks, (t1 - t0) * 1000 / freq,
           payload_ok(ticks == TICKS && t1 - t0 >= TICKS * period));
    printf("A6: done\n");
}
