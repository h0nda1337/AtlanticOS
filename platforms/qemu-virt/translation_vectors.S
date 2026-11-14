/* SPDX-License-Identifier: MIT - original QEMU-only EL1 fault observations. */
.section .vectors.translation, "ax"
.balign 2048
.global atl_translation_vectors
atl_translation_vectors:
.rept 4
    b .Lunexpected
    .balign 128
.endr
    b .Lsync
    .balign 128
.rept 11
    b .Lunexpected
    .balign 128
.endr

.text
.global atl_translation_try_store
.type atl_translation_try_store, %function
atl_translation_try_store:
    adr x12, .Lstore_recover
    ldrb w1, [x0]
.Lstore_instruction:
    strb w1, [x0]
    mov x0, xzr
.Lstore_recover:
    ret
.size atl_translation_try_store, .-atl_translation_try_store

.global atl_translation_try_execute
.type atl_translation_try_execute, %function
atl_translation_try_execute:
    adr x12, .Lexecute_recover
    mov x11, x30
    blr x0
    mov x0, xzr
.Lexecute_recover:
    ret x11
.size atl_translation_try_execute, .-atl_translation_try_execute

/* Known original code for the independent data-page NX check. */
.global atl_translation_return, atl_translation_return_end
atl_translation_return:
    mov x0, #0x51
    ret
atl_translation_return_end:

.Lsync:
    mrs x1, ESR_EL1
    mrs x2, FAR_EL1
    mrs x3, ELR_EL1
    cmp x2, x0
    b.ne .Lunexpected
    and x4, x1, #0x3f
    cmp x4, #0xf             /* Level 3 permission fault. */
    b.ne .Lunexpected
    ubfx x4, x1, #26, #6
    cmp x4, #0x25            /* Data abort at current EL. */
    b.ne .Lcheck_execute
    tbz x1, #6, .Lunexpected /* WnR must be set. */
    adr x4, .Lstore_instruction
    cmp x3, x4
    b.ne .Lunexpected
    adr x4, .Lstore_recover
    b .Lrecover
.Lcheck_execute:
    cmp x4, #0x21            /* Instruction abort at current EL. */
    b.ne .Lunexpected
    cmp x3, x0
    b.ne .Lunexpected
    adr x4, .Lexecute_recover
.Lrecover:
    cmp x12, x4
    b.ne .Lunexpected
    msr ELR_EL1, x4
    mov x0, x1
    eret

.Lunexpected:
    mrs x0, ESR_EL1
    mrs x1, ELR_EL1
    mrs x2, FAR_EL1
    bl atl_entry_exception
1:
    wfe
    b 1b
