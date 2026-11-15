/* A6, Secure part: GICv3 setup that only the Secure world may do.
 *
 * With security enabled (GICD_CTLR.DS = 0) the interrupt *group* of each
 * INTID is Secure-only state: Non-secure software cannot make an interrupt
 * its own. So EL3 decides that the Non-secure EL1 physical timer belongs to
 * Group 1 Non-secure; the NS payload then enables and handles it, like a
 * rich OS with its GIC driver. */
#include "gic.h"
#include "mmio.h"
#include "printf.h"
#include "sysreg.h"

static void gicd_wait_rwp(void)
{
    while (mmio_read32(GICD_BASE + GICD_CTLR) & GICD_CTLR_RWP)
        ;
}

void gic_el3_init(void)
{
    uint32_t v;

    /* CPU interface: system registers at EL3, and let EL1 use ICC_SRE_EL1. */
    write_sysreg(ICC_SRE_SRE | ICC_SRE_DFB | ICC_SRE_DIB | ICC_SRE_EL3_ENABLE, icc_sre_el3);
    isb();

    /* Distributor: affinity routing must be on before the groups are enabled. */
    mmio_write32(GICD_BASE + GICD_CTLR, GICD_CTLR_ARE_S | GICD_CTLR_ARE_NS);
    gicd_wait_rwp();
    mmio_write32(GICD_BASE + GICD_CTLR, GICD_CTLR_ARE_S | GICD_CTLR_ARE_NS |
                 GICD_CTLR_EN_GRP0 | GICD_CTLR_EN_GRP1NS | GICD_CTLR_EN_GRP1S);
    gicd_wait_rwp();

    /* Redistributor of this CPU: it resets "asleep"; wake it up. */
    v = mmio_read32(GICR_BASE + GICR_WAKER);
    mmio_write32(GICR_BASE + GICR_WAKER, v & ~GICR_WAKER_PROC_SLEEP);
    while (mmio_read32(GICR_BASE + GICR_WAKER) & GICR_WAKER_CHILD_ASLEEP)
        ;

    /* Group 1 Non-secure = IGROUPR bit 1, IGRPMODR bit 0. */
    v = mmio_read32(GICR_SGI_BASE + GICR_IGROUPR0);
    mmio_write32(GICR_SGI_BASE + GICR_IGROUPR0, v | (1u << INTID_NS_EL1_PTIMER));
    v = mmio_read32(GICR_SGI_BASE + GICR_IGRPMODR0);
    mmio_write32(GICR_SGI_BASE + GICR_IGRPMODR0, v & ~(1u << INTID_NS_EL1_PTIMER));

    printf("A6: EL3: GICD_CTLR=0x%x GICR_WAKER=0x%x IGROUPR0=0x%x IGRPMODR0=0x%x\n",
           mmio_read32(GICD_BASE + GICD_CTLR), mmio_read32(GICR_BASE + GICR_WAKER),
           mmio_read32(GICR_SGI_BASE + GICR_IGROUPR0),
           mmio_read32(GICR_SGI_BASE + GICR_IGRPMODR0));
}
