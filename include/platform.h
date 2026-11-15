/* QEMU virt (secure=on) addresses, verified in docs/NOTES.md. */
#ifndef PLATFORM_H
#define PLATFORM_H

#define SECURE_RAM_BASE   0x0e000000UL   /* secure-only; EL3 .data starts here */
#define NS_RAM_BASE       0x40000000UL   /* payload load/link address */
#define GICD_BASE         0x08000000UL
#define GICR_BASE         0x080a0000UL   /* redistributor of CPU 0 (RD_base) */

#endif
