/* SPDX-License-Identifier: MIT - Original retained XNU bootstrap context. */
#ifndef ATLANTIC_BOOT_AUX_H
#define ATLANTIC_BOOT_AUX_H

#include <atlantic/boot_locator.h>
#include <atlantic/image_materialize.h>

struct AtlanticBootIdentity {
    uint8_t image_sha256[32], upstream_commit[20], port_id[32];
    uint64_t primary_mpidr, counter_frequency;
    uint32_t initial_el, cpu_count;
};

struct AtlanticRetainedContext {
    struct AtlanticBootIdentity identity;
    uint64_t image_pa, image_bytes, entry_pa, context_pa, context_bytes;
};

enum AtlanticBootAuxError {
    ATL_BOOT_AUX_ARGUMENT = -84,
    ATL_BOOT_AUX_ALIAS = -85,
    ATL_BOOT_AUX_LAYOUT = -86,
    ATL_BOOT_AUX_WIRE = -87
};

/* Install ATLC v1, SecureDT and native boot_args after image materialization.
 * The arena is the same quiescent, exclusive, owned RAM mapping. Preparation
 * comes from successful placement of the intended immutable image. The tree
 * comes from atl_xnu_boot_tree for that inventory and the observed primary CPU.
 * Identity digests are supplied by the caller after verification. All inputs
 * remain stable, including across DMA, and disjoint from the complete arena.
 *
 * Validates all placement before any write. Success zeros the dedicated
 * context, tree and argument slots, then writes their bounded payloads. The
 * image, prefix and remainder of the arena are untouched. The operation does
 * not authenticate the image, establish address translations or manage caches. */
int atl_xnu_boot_install_aux(const struct AtlanticMappedArena *arena,
                             const struct AtlanticBootPreparation *preparation,
                             const void *tree, size_t tree_bytes,
                             const struct AtlanticBootIdentity *identity);

/* Decode a readable ATLC trailer whose mapping has already been proven by the
 * caller. The locator comes from validated boot_args. Match numeric placement,
 * leave output untouched on error, and never dereference a derived address. */
int atl_xnu_boot_read_trailer(const struct AtlanticBootLocator *locator,
                              const void *trailer, size_t trailer_bytes,
                              struct AtlanticRetainedContext *out);

#endif
