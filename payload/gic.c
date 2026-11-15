/* A6, Non-secure part: the GIC "driver" of the payload. Group assignment was
 * done by EL3 (src/gic_el3.c); here only Non-secure-accessible state is set. */
#include "gic.h"
#include "gic_ns.h"
#include "mmio.h"
#include "sysreg.h"

void gic_cpu_init(void)
{
    write_sysreg(ICC_SRE_SRE, icc_sre_el1);   /* system-register interface */
    isb();
    write_sysreg(0xff, icc_pmr_el1);          /* priority mask: let everything through */
    write_sysreg(1, icc_igrpen1_el1);         /* enable Group 1 (NS) at this CPU interface */
    isb();
}

void gic_enable_ppi(unsigned int intid, uint8_t prio)
{
    uintptr_t reg = GICR_SGI_BASE + GICR_IPRIORITYR + (intid & ~3u);
    unsigned int shift = (intid & 3u) * 8;

    /* Priority registers hold one byte per INTID; update ours with a 32-bit RMW. */
    mmio_write32(reg, (mmio_read32(reg) & ~(0xffu << shift)) | ((uint32_t)prio << shift));
    mmio_write32(GICR_SGI_BASE + GICR_ISENABLER0, 1u << intid);   /* write-1-to-set */
}

uint32_t gic_ack(void)
{
    return (uint32_t)read_sysreg(icc_iar1_el1) & 0xffffff;   /* INTID, activates it */
}

void gic_eoi(uint32_t intid)
{
    write_sysreg(intid, icc_eoir1_el1);   /* priority drop + deactivate */
}
