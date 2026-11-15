/* EL1 payload entry. EL3 "returns" here with eret, in EL1h with DAIF masked,
 * MMU and caches off. Same job as the EL3 start.S: stack, BSS, vectors. */
    .section .text.boot, "ax"
    .global _start
_start:
    ldr     x1, =__stack_top
    mov     sp, x1              /* SP_EL1: EL1 has its own stack pointer */

    ldr     x1, =__bss_start
    ldr     x2, =__bss_end
1:  cmp     x1, x2
    b.hs    2f
    stp     xzr, xzr, [x1], #16
    b       1b

2:  ldr     x1, =el1_vectors
    msr     vbar_el1, x1
    isb
    bl      payload_main
3:  wfe
    b       3b
