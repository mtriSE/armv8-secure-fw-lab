/* A5: turn on the EL1 MMU (Non-secure EL1&0 translation regime) with one
 * hand-written level-1 table: 4KB granule, 32-bit VA (T0SZ=32), so the walk
 * starts at level 1 and each of the 4 entries maps a 1GB block.
 *
 *   VA 0x0000_0000 -> PA 0x0000_0000  Device-nGnRnE, never executable (UART, GIC)
 *   VA 0x4000_0000 -> PA 0x4000_0000  Normal write-back cacheable (payload RAM)
 *   VA 0x8000_0000    unmapped (translation fault test)
 *   VA 0xC000_0000 -> PA 0x4000_0000  alias of the RAM block (proves VA != PA works)
 */
#include "mmu.h"
#include "sysreg.h"

/* MAIR_EL1: up to 8 memory types; descriptors pick one by index. */
#define MAIR_DEVICE_nGnRnE  0x00UL   /* no Gathering, no Reordering, no Early ack */
#define MAIR_NORMAL_WB      0xffUL   /* inner/outer write-back, read/write-allocate */
#define ATTR_DEVICE         0
#define ATTR_NORMAL         1

/* Level-1 block descriptor fields (Arm ARM, VMSAv8-64 descriptors). */
#define DESC_BLOCK          0x1UL            /* bits[1:0] = 01: block */
#define DESC_ATTRIDX(i)     ((uint64_t)(i) << 2)
#define DESC_AP_RW_EL1      (0UL << 6)       /* EL1 read/write, EL0 no access */
#define DESC_SH_INNER       (3UL << 8)
#define DESC_AF             (1UL << 10)      /* access flag: 0 would fault on first use */
#define DESC_PXN            (1UL << 53)
#define DESC_UXN            (1UL << 54)

/* TCR_EL1 */
#define TCR_T0SZ(n)         ((uint64_t)(n))  /* VA size = 64 - n bits */
#define TCR_IRGN0_WBWA      (1UL << 8)       /* table walks: inner write-back */
#define TCR_ORGN0_WBWA      (1UL << 10)
#define TCR_SH0_INNER       (3UL << 12)
#define TCR_TG0_4K          (0UL << 14)
#define TCR_EPD1            (1UL << 23)      /* no TTBR1 (upper VA range) walks */
#define TCR_IPS_32BIT       (0UL << 32)      /* 4GB physical address space */

#define SCTLR_M             (1UL << 0)       /* MMU enable */
#define SCTLR_C             (1UL << 2)       /* data cache enable */
#define SCTLR_I             (1UL << 12)      /* instruction cache enable */

/* The table base must be aligned to the table size; 4KB is always enough. */
static uint64_t l1_table[4] __attribute__((aligned(4096)));

static uint64_t block(uint64_t pa, int attr)
{
    uint64_t d = pa | DESC_BLOCK | DESC_AF | DESC_AP_RW_EL1 | DESC_ATTRIDX(attr);

    if (attr == ATTR_DEVICE)
        d |= DESC_PXN | DESC_UXN;   /* never fetch instructions from devices */
    else
        d |= DESC_SH_INNER;
    return d;
}

void mmu_enable(void)
{
    l1_table[0] = block(0x00000000UL, ATTR_DEVICE);
    l1_table[1] = block(0x40000000UL, ATTR_NORMAL);
    l1_table[2] = 0;                                  /* invalid */
    l1_table[3] = block(0x40000000UL, ATTR_NORMAL);   /* alias */

    /* Table writes must reach memory before the walker can read them. */
    __asm__ volatile("dsb sy" ::: "memory");

    write_sysreg(MAIR_DEVICE_nGnRnE << (8 * ATTR_DEVICE) |
                 MAIR_NORMAL_WB << (8 * ATTR_NORMAL), mair_el1);
    write_sysreg(TCR_T0SZ(32) | TCR_IRGN0_WBWA | TCR_ORGN0_WBWA | TCR_SH0_INNER |
                 TCR_TG0_4K | TCR_EPD1 | TCR_IPS_32BIT, tcr_el1);
    write_sysreg(l1_table, ttbr0_el1);
    isb();

    /* Drop any stale TLB entries, e.g. left by the Secure run (EL1 state is
     * shared between worlds). */
    __asm__ volatile("tlbi vmalle1\n\tdsb nsh\n\tisb" ::: "memory");

    write_sysreg(read_sysreg(sctlr_el1) | SCTLR_M | SCTLR_C | SCTLR_I, sctlr_el1);
    isb();   /* following instructions are fetched with the MMU on */
}

uint64_t mmu_alias(uint64_t ram_va)
{
    return ram_va - 0x40000000UL + 0xC0000000UL;
}
