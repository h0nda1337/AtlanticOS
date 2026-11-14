/* SPDX-License-Identifier: MIT - Original static-image byte materialization. */
#ifndef ATLANTIC_IMAGE_MATERIALIZE_H
#define ATLANTIC_IMAGE_MATERIALIZE_H
#include <atlantic/boot_prepare.h>

struct AtlanticMappedArena {
    void *base;
    uint64_t physical_base;
    size_t bytes;
};

struct AtlanticRetainedImage {
    const void *base;
    uint64_t physical_base;
    size_t bytes;
};

enum AtlanticImageMaterializeError {
    ATL_MATERIALIZE_ARGUMENT = -70,
    ATL_MATERIALIZE_ALIAS = -71,
    ATL_MATERIALIZE_ARENA = -72,
    ATL_MATERIALIZE_SOURCE = -73,
    ATL_MATERIALIZE_LAYOUT = -74
};

/* The caller supplies the exact managed window as one existing, byte-addressable
 * writable mapping of owned ordinary RAM and retains readable, stable image
 * bytes in the inventory's RAM bank. The image's declared physical span must
 * be disjoint from the entire arena. The two physical_base values independently
 * describe their mappings. There is no conversion from a C pointer to a PA.
 * C pointer ranges reject visible aliases. The caller must establish actual PA
 * ownership, absence of hidden mapping aliases, and sufficient mapped capacity.
 * All descriptors, the command line and image bytes stay immutable throughout
 * the call. The caller provides exclusive writes and a quiescent destination
 * for the arena and out, including concurrent CPUs and DMA.
 *
 * Success revalidates the Mach-O and boot preparation, copies each segment's
 * file bytes and zeros its remainder through mapping_bytes, then returns
 * preparation in out. Holes, the pre-image prefix, context, tree, boot_args space
 * and the arena tail retain their bytes. The boot_args record lives only in out.
 * Errors from this function or validation leave the full arena and out untouched
 * under the mapping premises. The operation uses no heap, libc, cache or MMU
 * management. Authentication, readback, cache maintenance, translation,
 * permissions and authorization to enter the image belong to the caller. */
int atl_xnu_image_materialize(const struct AtlanticBootInventory *inventory,
                              const struct AtlanticRetainedImage *source,
                              const struct AtlanticBootRequest *request,
                              const struct AtlanticMappedArena *arena,
                              struct AtlanticBootPreparation *out);
#endif
