/* A1: reset entry at EL3 (secure flash, address 0). */
    .section .text.boot, "ax"
    .global _start
_start:
    /* Only core 0 continues; park the others. */
    mrs     x0, mpidr_el1
    and     x0, x0, #0xff
    cbnz    x0, park

    ldr     x0, =__stack_top
    mov     sp, x0

    /* Copy .data from flash (LMA) to secure RAM (VMA). */
    ldr     x0, =__data_load
    ldr     x1, =__data_start
    ldr     x2, =__data_end
1:  cmp     x1, x2
    b.hs    2f
    ldr     x3, [x0], #8
    str     x3, [x1], #8
    b       1b

    /* Clear BSS (16-byte aligned by the linker script). */
2:  ldr     x0, =__bss_start
    ldr     x1, =__bss_end
3:  cmp     x0, x1
    b.hs    4f
    stp     xzr, xzr, [x0], #16
    b       3b

    /* Install the EL3 vector table before running any C code, so a fault
     * is reported instead of jumping to an unknown VBAR_EL3. */
4:  ldr     x0, =el3_vectors
    msr     vbar_el3, x0
    isb
    bl      main
park:
    wfe
    b       park
