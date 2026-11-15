/* void el3_eret(uint64_t entry, uint64_t spsr, uint64_t arg)
 *
 * Drop from EL3 to a lower EL. There is no "jump to EL1" instruction: we
 * fake a return from an exception. ELR_EL3 = where to "return" to,
 * SPSR_EL3 = the PSTATE to restore (target EL, SP select, DAIF), then eret.
 * SCR_EL3 (world, register width) must already be set by the caller. */
    .text
    .global el3_eret
el3_eret:
    msr     elr_el3, x0
    msr     spsr_el3, x1
    mov     x0, x2
    /* Do not leak EL3 register contents to the lower EL. */
    mov     x1, xzr
    mov     x2, xzr
    mov     x3, xzr
    mov     x4, xzr
    mov     x5, xzr
    mov     x6, xzr
    mov     x7, xzr
    mov     x8, xzr
    mov     x9, xzr
    mov     x10, xzr
    mov     x11, xzr
    mov     x12, xzr
    mov     x13, xzr
    mov     x14, xzr
    mov     x15, xzr
    mov     x16, xzr
    mov     x17, xzr
    mov     x18, xzr
    mov     x19, xzr
    mov     x20, xzr
    mov     x21, xzr
    mov     x22, xzr
    mov     x23, xzr
    mov     x24, xzr
    mov     x25, xzr
    mov     x26, xzr
    mov     x27, xzr
    mov     x28, xzr
    mov     x29, xzr
    mov     x30, xzr
    eret
