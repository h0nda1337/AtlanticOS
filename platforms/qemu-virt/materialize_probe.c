/* SPDX-License-Identifier: MIT - QEMU-only byte placement and independent readback. */
#include <atlantic/entry.h>
#include <atlantic/image_materialize.h>
#if !ATL_TEST_MATERIALIZE
#error Compile this probe only for the explicit QEMU materialization mode
#endif

extern const uint8_t atl_probe_macho_start[], atl_probe_macho_end[];

struct FixtureSegment {
    size_t destination, file_offset, file_bytes, mapping_bytes;
};

/* Fixed expectations from independent load-command inspection of high.macho.
 * Relative to the start of the prepared image, including its BSS and padding. */
static const struct FixtureSegment segments[] = {
    {0x00000, 0x0000, 0x4000, 0x4000},
    {0x04000, 0x4000, 0x4000, 0x4000},
    {0x08000, 0x8000, 0x4000, 0x8000},
    {0x10000, 0xc000, 0x0130, 0x4000},
};

static int all_bytes(const volatile uint8_t *base, size_t count, uint8_t byte) {
    for (size_t i = 0; i < count; ++i)
        if (base[i] != byte) return 0;
    return 1;
}

static int fail(const char *stage) {
    (void)atlantic_early_platform.write(stage);
    return 1;
}

int atl_materialize_probe(const struct AtlanticBootInventory *inventory, uint32_t tree_bytes) {
    const size_t prefix = 0x4000, image_bytes = 0x14000, auxiliary_bytes = 0xc000;
    const uint8_t unwritten = 0xa7;
    struct AtlanticMappedArena arena = {
        .base = (void *)(uintptr_t)0x42000000, .physical_base = 0x42000000, .bytes = 0x2000000,
    };
    struct AtlanticBootRequest request = {
        .managed_pa = arena.physical_base, .managed_bytes = arena.bytes,
        .tree_bytes = tree_bytes, .retained_bytes = 4096, .command = "-v", .command_bytes = 2,
    };
    const struct AtlanticRetainedImage source = {
        .base = atl_probe_macho_start, .physical_base = (uintptr_t)atl_probe_macho_start,
        .bytes = (uintptr_t)atl_probe_macho_end - (uintptr_t)atl_probe_macho_start,
    };
    struct AtlanticBootPreparation result;
    volatile uint8_t *ram = arena.base;
    uint8_t *result_bytes = (uint8_t *)&result;
    const size_t seeded_bytes = prefix + image_bytes + auxiliary_bytes;

    /* The one-vCPU board with MMU off owns the proven 32 MiB lab window. Confirm
     * the reservation checks and the exact fixture profile before any RAM write. */
    if (source.bytes != 0xc130 ||
        atl_xnu_boot_prepare(inventory, source.base, source.bytes, &request, &result) ||
        result.image_pa != arena.physical_base + prefix ||
        result.image_bytes != image_bytes ||
        result.top_of_data != arena.physical_base + seeded_bytes ||
        result.entry_va != UINT64_C(0xfffffff007004498))
        return fail("FAIL:MATERIALIZE_FIXTURE_PROFILE\n");

    for (size_t i = 0; i < seeded_bytes; ++i) ram[i] = unwritten;
    ram[arena.bytes - 1] = unwritten;
    for (size_t i = 0; i < sizeof(result); ++i) result_bytes[i] = unwritten;

    /* The 64 MiB declaration crosses QEMU's actual DTB reserved by the entry. */
    struct AtlanticMappedArena rejected = arena;
    rejected.bytes *= 2;
    request.managed_bytes = rejected.bytes;
    if (atl_xnu_image_materialize(inventory, &source, &request, &rejected, &result) !=
        ATL_PREPARE_RESERVED || !all_bytes(ram, seeded_bytes, unwritten) ||
        ram[arena.bytes - 1] != unwritten || !all_bytes(result_bytes, sizeof(result), unwritten))
        return fail("FAIL:MATERIALIZE_RESERVED_WINDOW\n");
    (void)atlantic_early_platform.write("PASS:MATERIALIZE_RESERVED_WINDOW_UNTOUCHED\n");

    request.managed_bytes = arena.bytes;
    if (atl_xnu_image_materialize(inventory, &source, &request, &arena, &result) ||
        result.image_pa != arena.physical_base + prefix || result.image_bytes != image_bytes ||
        result.top_of_data != arena.physical_base + seeded_bytes ||
        result.entry_pa != arena.physical_base + 0x4498 ||
        result.entry_va != UINT64_C(0xfffffff007004498))
        return fail("FAIL:MATERIALIZE_RETURNED_PLAN\n");

    /* Force genuine readback after writes, with no execution of fixture code. */
    for (size_t i = 0; i < sizeof(segments) / sizeof(segments[0]); ++i) {
        const struct FixtureSegment *segment = &segments[i];
        const volatile uint8_t *dest = ram + prefix + segment->destination;
        const uint8_t *file = atl_probe_macho_start + segment->file_offset;
        for (size_t j = 0; j < segment->file_bytes; ++j)
            if (dest[j] != file[j]) return fail("FAIL:MATERIALIZE_SEGMENT_BYTES\n");
        if (!all_bytes(dest + segment->file_bytes, segment->mapping_bytes - segment->file_bytes, 0))
            return fail("FAIL:MATERIALIZE_SEGMENT_ZERO_FILL\n");
    }
    (void)atlantic_early_platform.write("PASS:MATERIALIZE_SEGMENT_READBACK\n");
    if (!all_bytes(ram, prefix, unwritten) ||
        !all_bytes(ram + prefix + image_bytes, auxiliary_bytes, unwritten) ||
        ram[arena.bytes - 1] != unwritten)
        return fail("FAIL:MATERIALIZE_UNWRITTEN_RAM\n");
    (void)atlantic_early_platform.write("PASS:MATERIALIZE_UNWRITTEN_RAM\nFIXTURE_ENTRY_NOT_EXECUTED\n");
    return 0;
}
