/* SPDX-License-Identifier: MIT - AtlanticOS native boot record locator. */
#include <atlantic/boot_locator.h>

_Static_assert(ATL_XNU_BOOT_TRAILER_BYTES <= ATL_XNU_BOOT_AUX_PAGE_BYTES &&
               ATL_XNU_BOOT_TRAILER_BYTES % 8 == 0, "unaligned boot trailer");

static uint32_t read_le32(const uint8_t *bytes)
{
    return bytes[0] | (uint32_t)bytes[1] << 8 |
           (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static uint64_t read_le64(const uint8_t *bytes)
{
    return read_le32(bytes) | (uint64_t)read_le32(bytes + 4) << 32;
}

int atl_xnu_boot_locate(const void *boot_args, size_t boot_args_bytes,
                       struct AtlanticBootLocator *out)
{
    const uint64_t block = ATL_XNU_BOOT_BLOCK_BYTES;
    const uint64_t aux = ATL_XNU_BOOT_AUX_PAGE_BYTES;
    const uint64_t identity_limit = UINT64_C(0x8000000000);
    const uint64_t image_offset = ATL_XNU_BOOT_LINK_BASE & (block - 1);
    const uint8_t *record = boot_args;
    uintptr_t input = (uintptr_t)boot_args;
    uintptr_t output = (uintptr_t)out;
    uint64_t physical_base, virtual_base, managed_bytes, top_of_data;
    uint64_t bootstrap_bytes, top_offset, tree_offset, tree_va;
    uint32_t tree_bytes;

    if (!record || !out || output % _Alignof(struct AtlanticBootLocator) ||
        boot_args_bytes != ATL_XNU_BOOT_RECORD_BYTES ||
        boot_args_bytes > UINTPTR_MAX - input || sizeof(*out) > UINTPTR_MAX - output ||
        (input <= output ? output - input < boot_args_bytes :
                           input - output < sizeof(*out)))
        return ATL_BOOT_LOCATOR_ARGUMENT;

    /* Packed here only for reading the two adjacent LE 16-bit fields. */
    if (read_le32(record) != 0x00020002u ||
        read_le32(record + 88) != ATL_XNU_BOOT_MACHINE_TYPE)
        return ATL_BOOT_LOCATOR_ABI;

    virtual_base = read_le64(record + 8);
    physical_base = read_le64(record + 16);
    managed_bytes = read_le64(record + 24);
    top_of_data = read_le64(record + 32);
    if (virtual_base != ATL_XNU_BOOT_LINK_BASE - image_offset ||
        (physical_base & (block - 1)) || (managed_bytes & (block - 1)) ||
        managed_bytes < 2 * block || managed_bytes > ATL_MACHO_MAX_BYTES ||
        physical_base >= identity_limit || managed_bytes > identity_limit - physical_base ||
        managed_bytes > UINT64_MAX - virtual_base ||
        top_of_data < physical_base || (top_of_data & (aux - 1)))
        return ATL_BOOT_LOCATOR_RANGE;

    /* Cold native entry subtracts the header offset before flooring the
     * block count. The first block starts at the aligned bases above. */
    bootstrap_bytes = (managed_bytes - image_offset) & ~(block - 1);
    top_offset = top_of_data - physical_base;
    /* Minimum of one image page, one context page, tree and arguments.
     * The real image end requires a separate check against the image plan. */
    if (top_offset < image_offset + ATL_MACHO_PAGE_BYTES + 3 * aux ||
        top_offset > bootstrap_bytes - aux)
        return ATL_BOOT_LOCATOR_RANGE;

    tree_offset = top_offset - 2 * aux;
    tree_va = virtual_base + tree_offset;
    tree_bytes = read_le32(record + 104);
    if (read_le64(record + 96) != tree_va || tree_bytes < 8 ||
        tree_bytes > ATL_XNU_TREE_MAX_BYTES)
        return ATL_BOOT_LOCATOR_TREE;

    out->managed_pa = physical_base;
    out->managed_bytes = managed_bytes;
    out->virtual_base = virtual_base;
    out->bootstrap_bytes = bootstrap_bytes;
    out->top_of_data = top_of_data;
    out->tree_pa = physical_base + tree_offset;
    out->tree_va = tree_va;
    out->trailer_pa = out->tree_pa - ATL_XNU_BOOT_TRAILER_BYTES;
    out->trailer_va = tree_va - ATL_XNU_BOOT_TRAILER_BYTES;
    out->boot_args_pa = top_of_data - aux;
    out->boot_args_va = tree_va + aux;
    out->tree_bytes = tree_bytes;
    return 0;
}
