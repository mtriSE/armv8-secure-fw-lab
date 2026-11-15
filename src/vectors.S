/* A3: EL3 exception vector table.
 * 16 entries of 0x80 bytes (32 instructions), table aligned to 2KB because
 * VBAR_EL3 bits [10:0] are RES0. */
#include "exception.h"

/* Each entry: make room for a trap frame, save x0/x1, pass the entry
 * number in x0, branch to common code (32 instructions is not enough
 * for the full save). */
.macro ventry num
    .align 7
    sub     sp, sp, #TF_SIZE
    stp     x0, x1, [sp, #16 * 0]
    mov     x0, #\num
    b       el3_trap_common
.endm

    .section .text.vectors, "ax"
    .align 11
    .global el3_vectors
el3_vectors:
    /* Current EL, SP_EL0 */
    ventry VEC_CUR_SP0_SYNC
    ventry VEC_CUR_SP0_IRQ
    ventry VEC_CUR_SP0_FIQ
    ventry VEC_CUR_SP0_SERROR
    /* Current EL, SP_ELx: faults in our own EL3 code land here */
    ventry VEC_CUR_SPX_SYNC
    ventry VEC_CUR_SPX_IRQ
    ventry VEC_CUR_SPX_FIQ
    ventry VEC_CUR_SPX_SERROR
    /* Lower EL, AArch64: smc from EL1 lands here (A4) */
    ventry VEC_LOW64_SYNC
    ventry VEC_LOW64_IRQ
    ventry VEC_LOW64_FIQ
    ventry VEC_LOW64_SERROR
    /* Lower EL, AArch32: unused */
    ventry VEC_LOW32_SYNC
    ventry VEC_LOW32_IRQ
    ventry VEC_LOW32_FIQ
    ventry VEC_LOW32_SERROR

    .text
el3_trap_common:
    /* x0 = vector number, x0/x1 of the interrupted code already saved. */
    stp     x2, x3, [sp, #16 * 1]
    stp     x4, x5, [sp, #16 * 2]
    stp     x6, x7, [sp, #16 * 3]
    stp     x8, x9, [sp, #16 * 4]
    stp     x10, x11, [sp, #16 * 5]
    stp     x12, x13, [sp, #16 * 6]
    stp     x14, x15, [sp, #16 * 7]
    stp     x16, x17, [sp, #16 * 8]
    stp     x18, x19, [sp, #16 * 9]
    stp     x20, x21, [sp, #16 * 10]
    stp     x22, x23, [sp, #16 * 11]
    stp     x24, x25, [sp, #16 * 12]
    stp     x26, x27, [sp, #16 * 13]
    stp     x28, x29, [sp, #16 * 14]
    mrs     x1, elr_el3
    stp     x30, x1, [sp, #TF_X30]
    mrs     x1, spsr_el3
    str     x1, [sp, #TF_SPSR]

    mov     x1, sp                  /* arg1: struct trap_frame * */
    bl      el3_exception_handler   /* arg0: vector number (x0) */

    /* The handler may have changed elr/spsr in the frame. */
    ldp     x30, x1, [sp, #TF_X30]
    msr     elr_el3, x1
    ldr     x1, [sp, #TF_SPSR]
    msr     spsr_el3, x1
    ldp     x2, x3, [sp, #16 * 1]
    ldp     x4, x5, [sp, #16 * 2]
    ldp     x6, x7, [sp, #16 * 3]
    ldp     x8, x9, [sp, #16 * 4]
    ldp     x10, x11, [sp, #16 * 5]
    ldp     x12, x13, [sp, #16 * 6]
    ldp     x14, x15, [sp, #16 * 7]
    ldp     x16, x17, [sp, #16 * 8]
    ldp     x18, x19, [sp, #16 * 9]
    ldp     x20, x21, [sp, #16 * 10]
    ldp     x22, x23, [sp, #16 * 11]
    ldp     x24, x25, [sp, #16 * 12]
    ldp     x26, x27, [sp, #16 * 13]
    ldp     x28, x29, [sp, #16 * 14]
    ldp     x0, x1, [sp, #16 * 0]
    add     sp, sp, #TF_SIZE
    eret
