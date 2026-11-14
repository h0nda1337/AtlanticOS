/* SPDX-License-Identifier: MIT - AtlanticOS native boot record locator. */
#ifndef ATLANTIC_BOOT_LOCATOR_H
#define ATLANTIC_BOOT_LOCATOR_H

#include <atlantic/boot_prepare.h>

#define ATL_XNU_BOOT_TRAILER_BYTES 256u

enum AtlanticBootLocatorError {
    ATL_BOOT_LOCATOR_ARGUMENT = -80,
    ATL_BOOT_LOCATOR_ABI = -81,
    ATL_BOOT_LOCATOR_RANGE = -82,
    ATL_BOOT_LOCATOR_TREE = -83
};

struct AtlanticBootLocator {
    uint64_t managed_pa, managed_bytes, virtual_base, bootstrap_bytes, top_of_data;
    uint64_t tree_pa, tree_va, trailer_pa, trailer_va, boot_args_pa, boot_args_va;
    uint32_t tree_bytes;
};

/* Derive addresses from the pinned ATL1 little-endian XNU boot_args record.
 * The input may be the immutable native copy. boot_args_pa/va describe the
 * original placement, not the address of the input span. The trailer ends
 * where the tree slot starts. The caller supplies exactly one readable record
 * and a distinct, suitably aligned writable output. Failure leaves both
 * untouched.
 *
 * Success validates the profile and reconstructs numeric bootstrap reach.
 * The function never follows the resulting addresses. The caller still needs
 * to prove image separation, ownership and each relevant live translation
 * before touching those spans. No heap, libc or MMU access. */
int atl_xnu_boot_locate(const void *boot_args, size_t boot_args_bytes,
                       struct AtlanticBootLocator *out);

#endif
