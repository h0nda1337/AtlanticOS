/* SPDX-License-Identifier: MIT */
.section .vectors, "ax"
.balign 2048
.global atl_irq_probe_vectors
atl_irq_probe_vectors:
.macro slot target
    b \target
    .balign 128
.endm
.rept 5
    slot atl_entry_trap
.endr
    slot atl_irq_vector_el1h
.rept 10
    slot atl_entry_trap
.endr
atl_irq_vector_el1h:
    /* Preserve every GPR that an ordinary C call may clobber. General-regs-only. */
    sub sp, sp, #160
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x30, [sp, #144]
    mrs x0, ELR_EL1
    bl atl_irq_probe_handler
    ldp x18, x30, [sp, #144]
    ldp x16, x17, [sp, #128]
    ldp x14, x15, [sp, #112]
    ldp x12, x13, [sp, #96]
    ldp x10, x11, [sp, #80]
    ldp x8, x9, [sp, #64]
    ldp x6, x7, [sp, #48]
    ldp x4, x5, [sp, #32]
    ldp x2, x3, [sp, #16]
    ldp x0, x1, [sp, #0]
    add sp, sp, #160
    eret
