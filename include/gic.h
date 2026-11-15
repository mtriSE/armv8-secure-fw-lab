/* GICv3 register offsets used by EL3 (src/gic_el3.c) and the payload
 * (payload/gic.c). Arm IHI 0069 (GICv3/v4 architecture specification). */
#ifndef GIC_H
#define GIC_H

#include "platform.h"

/* Distributor */
#define GICD_CTLR               0x0000
#define GICD_CTLR_EN_GRP0       (1u << 0)
#define GICD_CTLR_EN_GRP1NS     (1u << 1)
#define GICD_CTLR_EN_GRP1S      (1u << 2)
#define GICD_CTLR_ARE_S         (1u << 4)    /* affinity routing, Secure */
#define GICD_CTLR_ARE_NS        (1u << 5)    /* affinity routing, Non-secure */
#define GICD_CTLR_RWP           (1u << 31)   /* register write pending */

/* Redistributor: RD_base frame, then SGI_base frame 64KB above it.
 * SGIs (INTID 0-15) and PPIs (16-31) are per-CPU, so they are configured
 * here, not in the Distributor. */
#define GICR_WAKER              0x0014
#define GICR_WAKER_PROC_SLEEP   (1u << 1)
#define GICR_WAKER_CHILD_ASLEEP (1u << 2)
#define GICR_SGI_BASE           (GICR_BASE + 0x10000)
#define GICR_IGROUPR0           0x0080
#define GICR_ISENABLER0         0x0100
#define GICR_IPRIORITYR         0x0400       /* one byte per INTID */
#define GICR_IGRPMODR0          0x0d00

/* ICC_SRE_ELx: use the system-register CPU interface */
#define ICC_SRE_SRE             (1UL << 0)
#define ICC_SRE_DFB             (1UL << 1)
#define ICC_SRE_DIB             (1UL << 2)
#define ICC_SRE_EL3_ENABLE      (1UL << 3)   /* lower ELs may access ICC_SRE_EL1/EL2 */

/* Non-secure EL1 physical timer: PPI 14 in the DT timer node -> INTID 16+14 */
#define INTID_NS_EL1_PTIMER     30
#define INTID_SPURIOUS          1023

#endif
