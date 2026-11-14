/* SPDX-License-Identifier: MIT - native headers and target-only ABI checks. */
#include <libkern/Block_private.h>
__BEGIN_DECLS
#include <kern/kalloc.h>
__END_DECLS

#ifdef MACH_KERNEL_PRIVATE
#error This probe requires the native libkern private context
#endif
#if !LIBKERN_KERNEL_PRIVATE || !CONFIG_ATLANTIC_C_LAYOUT || !CONFIG_ATLANTIC_CPP_LAYOUT
#error Both separately proved layout APIs and libkern context are required
#endif
#ifdef __cplusplus
#define ASSERT static_assert
#else
#define ASSERT _Static_assert
#endif
#ifdef ATL_TEST_WRONG_BLOCK_ABI
ASSERT(sizeof(struct Block_layout) == 24, "Block_layout target ABI mismatch");
#else
ASSERT(sizeof(struct Block_layout) == 32, "Block_layout target ABI mismatch");
#endif
ASSERT(sizeof(struct Block_byref) == 24, "Block_byref target ABI mismatch");
ASSERT(__alignof__(struct Block_layout) == 8 && __alignof__(struct Block_byref) == 8,
    "Block alignment");
ASSERT(__builtin_offsetof(struct Block_layout, flags) == 8 &&
    __builtin_offsetof(struct Block_layout, invoke) == 16 &&
    __builtin_offsetof(struct Block_layout, descriptor) == 24, "Block field offsets");
ASSERT(__builtin_offsetof(struct Block_byref, forwarding) == 8 &&
    __builtin_offsetof(struct Block_byref, flags) == 16 &&
    __builtin_offsetof(struct Block_byref, size) == 20, "byref field offsets");
ASSERT(sizeof(_NSConcreteStackBlock) == 256 && sizeof(_NSConcreteGlobalBlock) == 256,
    "Native class placeholder sizes");
#ifdef ATL_TEST_WRONG_BLOCK_METADATA
ASSERT(__builtin_strcmp(KALLOC_LAYOUT_SIGNATURE(struct Block_layout), "2222") == 0,
    "Block pointer metadata mismatch");
#else
ASSERT(__builtin_strcmp(KALLOC_LAYOUT_SIGNATURE(struct Block_layout), "1211") == 0,
    "Block pointer metadata mismatch");
#endif
ASSERT(__builtin_strcmp(KALLOC_LAYOUT_SIGNATURE(struct Block_byref), "112") == 0,
    "Byref pointer metadata mismatch");
ASSERT(KALLOC_LAYOUT_SUMMARY(struct Block_layout) == 6 &&
    KALLOC_LAYOUT_SUMMARY(struct Block_byref) == 6, "Native pointer/data summary");
ASSERT(!KALLOC_TYPE_IS_DATA_ONLY(struct Block_layout), "Block has real pointer fields");

KALLOC_TYPE_VAR_DEFINE(ATL_BLOCK_LAYOUT, struct Block_layout, void *, KT_DEFAULT);
KALLOC_TYPE_VAR_DEFINE(ATL_BLOCK_BYREF, struct Block_byref, void *, KT_DEFAULT);

__BEGIN_DECLS
extern void *_Block_copy(const void *);
extern void _Block_release(const void *);
extern void _Block_object_assign(void *, const void *, const int);
extern void _Block_object_dispose(const void *, const int);
#define WIRE(name, value) \
    extern const unsigned long long atl_blocks_##name; \
    const unsigned long long atl_blocks_##name = (value)
WIRE(layout_bytes, sizeof(struct Block_layout));
WIRE(layout_align, __alignof__(struct Block_layout));
WIRE(layout_flags, __builtin_offsetof(struct Block_layout, flags));
WIRE(layout_invoke, __builtin_offsetof(struct Block_layout, invoke));
WIRE(layout_descriptor, __builtin_offsetof(struct Block_layout, descriptor));
WIRE(byref_bytes, sizeof(struct Block_byref));
WIRE(byref_align, __alignof__(struct Block_byref));
WIRE(byref_forwarding, __builtin_offsetof(struct Block_byref, forwarding));
WIRE(byref_flags, __builtin_offsetof(struct Block_byref, flags));
WIRE(byref_size, __builtin_offsetof(struct Block_byref, size));
WIRE(var_view_bytes, sizeof(struct kalloc_type_var_view));
WIRE(var_view_align, __alignof__(struct kalloc_type_var_view));
WIRE(var_view_flags, __builtin_offsetof(struct kalloc_type_var_view, kt_flags));
WIRE(var_view_sig_hdr, __builtin_offsetof(struct kalloc_type_var_view, kt_sig_hdr));
WIRE(var_view_sig_type, __builtin_offsetof(struct kalloc_type_var_view, kt_sig_type));
WIRE(layout_summary, KALLOC_LAYOUT_SUMMARY(struct Block_layout));
WIRE(byref_summary, KALLOC_LAYOUT_SUMMARY(struct Block_byref));
const char *atl_blocks_layout_signature = KALLOC_LAYOUT_SIGNATURE(struct Block_layout);
const char *atl_blocks_byref_signature = KALLOC_LAYOUT_SIGNATURE(struct Block_byref);
__END_DECLS
