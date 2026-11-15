/* Exception vector table generator, shared by EL3 (src/vectors.S) and the
 * EL1 payload (payload/vectors.S). The mechanism is identical at every EL;
 * only the ELR/SPSR register names and the C handler differ.
 *
 * Layout: 16 entries of 0x80 bytes (32 instructions), table aligned to 2KB
 * because VBAR_ELx bits [10:0] are RES0. */
#ifndef VECTOR_TABLE_H
#define VECTOR_TABLE_H

#include "exception.h"

#ifdef __ASSEMBLER__

/* Each entry: make room for a trap frame, save x0/x1, pass the entry
 * number in x0, branch to the common code (32 instructions is not enough
 * for the full save). */
.macro ventry num, common
    .align 7
    sub     sp, sp, #TF_SIZE
    stp     x0, x1, [sp, #16 * 0]
    mov     x0, #\num
    b       \common
.endm

/* vector_table <symbol>, <el1|el3>, <C handler(vec, struct trap_frame *)> */
.macro vector_table name, el, handler
    .section .text.vectors_\el, "ax"
    .align 11
    .global \name
\name:
    /* Current EL, SP_EL0 */
    ventry VEC_CUR_SP0_SYNC,   \name\()_common
    ventry VEC_CUR_SP0_IRQ,    \name\()_common
    ventry VEC_CUR_SP0_FIQ,    \name\()_common
    ventry VEC_CUR_SP0_SERROR, \name\()_common
    /* Current EL, SP_ELx: faults (and IRQs) in code running at this EL */
    ventry VEC_CUR_SPX_SYNC,   \name\()_common
    ventry VEC_CUR_SPX_IRQ,    \name\()_common
    ventry VEC_CUR_SPX_FIQ,    \name\()_common
    ventry VEC_CUR_SPX_SERROR, \name\()_common
    /* Lower EL, AArch64: e.g. smc from EL1 into EL3 */
    ventry VEC_LOW64_SYNC,     \name\()_common
    ventry VEC_LOW64_IRQ,      \name\()_common
    ventry VEC_LOW64_FIQ,      \name\()_common
    ventry VEC_LOW64_SERROR,   \name\()_common
    /* Lower EL, AArch32: unused */
    ventry VEC_LOW32_SYNC,     \name\()_common
    ventry VEC_LOW32_IRQ,      \name\()_common
    ventry VEC_LOW32_FIQ,      \name\()_common
    ventry VEC_LOW32_SERROR,   \name\()_common

    .text
\name\()_common:
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
    mrs     x1, elr_\el
    stp     x30, x1, [sp, #TF_X30]
    mrs     x1, spsr_\el
    str     x1, [sp, #TF_SPSR]

    mov     x1, sp                  /* arg1: struct trap_frame * */
    bl      \handler                /* arg0: vector number (x0) */

    /* The handler may have changed elr/spsr/x0.. in the frame. */
    ldp     x30, x1, [sp, #TF_X30]
    msr     elr_\el, x1
    ldr     x1, [sp, #TF_SPSR]
    msr     spsr_\el, x1
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
.endm

#endif /* __ASSEMBLER__ */
#endif
