/* SPDX-License-Identifier: MIT - Host tests for retained auxiliary boot data. */
#include <atlantic/boot_aux.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ARENA_BYTES = 4 * 1024 * 1024, GUARD = 32 };
static const uint64_t ARENA_PA = 0x42000000;
static unsigned rejected;

static void every_byte(const void *data, size_t bytes, uint8_t expected) {
    const uint8_t *p = data;
    for (size_t i = 0; i < bytes; ++i) assert(p[i] == expected);
}

static void store(uint8_t *data, uint64_t value, size_t bytes) {
    for (size_t i = 0; i < bytes; ++i) data[i] = (uint8_t)(value >> (i * 8));
}

static uint8_t *read_image(const char *path, size_t *bytes) {
    FILE *file = fopen(path, "rb");
    assert(file && !fseek(file, 0, SEEK_END));
    long end = ftell(file);
    assert(end > 0 && end < ATL_MACHO_MAX_BYTES);
    *bytes = (size_t)end;
    rewind(file);
    uint8_t *data = malloc(*bytes);
    assert(data && fread(data, 1, *bytes, file) == *bytes && !fclose(file));
    return data;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    size_t file_bytes;
    uint8_t *file = read_image(argv[1], &file_bytes);
    uint8_t *allocation = malloc(ARENA_BYTES + 2 * GUARD);
    assert(allocation);
    memset(allocation, 0xa5, ARENA_BYTES + 2 * GUARD);
    uint8_t *ram = allocation + GUARD;
    const struct AtlanticMappedArena arena = {ram, ARENA_PA, ARENA_BYTES};
    const struct AtlanticBootInventory inventory = {
        .ram = {{0x40000000, 0x8000000}}, .cpu_affinity = {0}, .ram_count = 1, .cpu_count = 1,
    };
    _Alignas(8) uint8_t tree[ATL_XNU_TREE_MAX_BYTES];
    size_t tree_bytes;
    assert(!atl_xnu_boot_tree(&inventory, 0, 62500000, tree, sizeof(tree), &tree_bytes));
    assert(tree_bytes == 560);
    const struct AtlanticBootRequest req = {ARENA_PA, ARENA_BYTES, 560, 32768, "-v", 2};
    struct AtlanticBootPreparation prep;
    assert(!atl_xnu_boot_prepare(&inventory, file, file_bytes, &req, &prep));
    struct AtlanticBootIdentity id = {.primary_mpidr = 0, .counter_frequency = 62500000,
                                     .initial_el = 2, .cpu_count = 1};
    memset(id.image_sha256, 0x11, 32);
    memset(id.upstream_commit, 0x22, 20);
    memset(id.port_id, 0x33, 32);

#define REJECT_INSTALL(invocation, error) do {                                      \
    assert((invocation) == (error));                                                 \
    every_byte(allocation, ARENA_BYTES + 2 * GUARD, 0xa5); ++rejected;                 \
} while (0)
    REJECT_INSTALL(atl_xnu_boot_install_aux(NULL, &prep, tree, tree_bytes, &id), ATL_BOOT_AUX_ARGUMENT);
    REJECT_INSTALL(atl_xnu_boot_install_aux(&arena, &prep, NULL, tree_bytes, &id), ATL_BOOT_AUX_ARGUMENT);
    REJECT_INSTALL(atl_xnu_boot_install_aux(&arena, &prep, tree, tree_bytes - 1, &id), ATL_BOOT_AUX_LAYOUT);
    REJECT_INSTALL(atl_xnu_boot_install_aux(&arena, &prep, ram, tree_bytes, &id), ATL_BOOT_AUX_ALIAS);
    struct AtlanticBootPreparation bad_prep = prep;
    bad_prep.context_pa += 16384;
    REJECT_INSTALL(atl_xnu_boot_install_aux(&arena, &bad_prep, tree, tree_bytes, &id), ATL_BOOT_AUX_LAYOUT);
    bad_prep = prep;
    bad_prep.entry_va += 4;
    REJECT_INSTALL(atl_xnu_boot_install_aux(&arena, &bad_prep, tree, tree_bytes, &id), ATL_BOOT_AUX_LAYOUT);
    bad_prep = prep;
    bad_prep.boot_args[0] = 1;
    REJECT_INSTALL(atl_xnu_boot_install_aux(&arena, &bad_prep, tree, tree_bytes, &id), ATL_BOOT_LOCATOR_ABI);
    struct AtlanticBootIdentity bad_id = id;
    bad_id.initial_el = 0;
    REJECT_INSTALL(atl_xnu_boot_install_aux(&arena, &prep, tree, tree_bytes, &bad_id), ATL_BOOT_AUX_LAYOUT);
    struct AtlanticMappedArena bad_arena = arena;
    bad_arena.bytes -= 1;
    REJECT_INSTALL(atl_xnu_boot_install_aux(&bad_arena, &prep, tree, tree_bytes, &id), ATL_BOOT_AUX_LAYOUT);
#undef REJECT_INSTALL

    assert(!atl_xnu_boot_install_aux(&arena, &prep, tree, tree_bytes, &id));
    size_t context_offset = prep.context_pa - ARENA_PA;
    size_t tree_offset = prep.tree_pa - ARENA_PA;
    size_t args_offset = prep.boot_args_pa - ARENA_PA;
    size_t top_offset = prep.top_of_data - ARENA_PA;
    const uint8_t *trailer = ram + tree_offset - ATL_XNU_BOOT_TRAILER_BYTES;
    every_byte(allocation, GUARD + context_offset, 0xa5);
    every_byte(ram + context_offset, tree_offset - ATL_XNU_BOOT_TRAILER_BYTES - context_offset, 0);
    assert(!memcmp(ram + tree_offset, tree, tree_bytes));
    every_byte(ram + tree_offset + tree_bytes, args_offset - tree_offset - tree_bytes, 0);
    assert(!memcmp(ram + args_offset, prep.boot_args, sizeof(prep.boot_args)));
    every_byte(ram + args_offset + sizeof(prep.boot_args), top_offset - args_offset - sizeof(prep.boot_args), 0);
    every_byte(ram + top_offset, ARENA_BYTES + GUARD - top_offset, 0xa5);

    uint8_t expected[ATL_XNU_BOOT_TRAILER_BYTES] = {0};
    memcpy(expected, "ATLC", 4);
    store(expected + 4, 1, 4); store(expected + 8, sizeof(expected), 4);
    store(expected + 12, tree_bytes, 4);
    const uint64_t fields[] = {ARENA_PA, ARENA_BYTES, 0xfffffff007000000, prep.top_of_data,
        prep.image_pa, 0x18000, prep.entry_pa, prep.context_pa, 32768, 0, 62500000};
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
        store(expected + 16 + i * 8, fields[i], 8);
    store(expected + 104, 2, 4); store(expected + 108, 1, 4);
    memset(expected + 112, 0x11, 32); memset(expected + 144, 0x22, 20); memset(expected + 164, 0x33, 32);
    assert(!memcmp(trailer, expected, sizeof(expected)));

    uint8_t native_copy[ATL_XNU_BOOT_RECORD_BYTES];
    memcpy(native_copy, ram + args_offset, sizeof(native_copy));
    struct AtlanticBootLocator loc;
    assert(!atl_xnu_boot_locate(native_copy, sizeof(native_copy), &loc));
    struct AtlanticRetainedContext decoded;
    assert(!atl_xnu_boot_read_trailer(&loc, trailer, ATL_XNU_BOOT_TRAILER_BYTES, &decoded));
    assert(decoded.image_pa == prep.image_pa && decoded.image_bytes == prep.image_bytes);
    assert(decoded.entry_pa == prep.entry_pa && decoded.context_pa == prep.context_pa && decoded.context_bytes == 32768);
    assert(decoded.identity.counter_frequency == 62500000 && decoded.identity.initial_el == 2);
    assert(decoded.identity.cpu_count == 1 && decoded.identity.primary_mpidr == 0);
    assert(!memcmp(decoded.identity.image_sha256, id.image_sha256, 32));
    assert(!memcmp(decoded.identity.upstream_commit, id.upstream_commit, 20));
    assert(!memcmp(decoded.identity.port_id, id.port_id, 32));

#define REJECT_READ(invocation, error) do {                                           \
    memset(&decoded, 0xa5, sizeof(decoded)); assert((invocation) == (error));         \
    every_byte(&decoded, sizeof(decoded), 0xa5); ++rejected;                          \
} while (0)
    REJECT_READ(atl_xnu_boot_read_trailer(&loc, trailer, 255, &decoded), ATL_BOOT_AUX_ARGUMENT);
    REJECT_READ(atl_xnu_boot_read_trailer(&loc, &decoded, 256, &decoded), ATL_BOOT_AUX_ALIAS);
    const size_t wire_errors[] = {0, 6, 8, 12, 16, 24, 32, 40, 196, 255};
    for (size_t i = 0; i < sizeof(wire_errors) / sizeof(wire_errors[0]); ++i) {
        memcpy(expected, trailer, sizeof(expected));
        expected[wire_errors[i]] ^= 1;
        REJECT_READ(atl_xnu_boot_read_trailer(&loc, expected, sizeof(expected), &decoded), ATL_BOOT_AUX_WIRE);
    }
    const size_t layout_errors[] = {48, 56, 64, 72, 80, 91, 100, 104, 108};
    for (size_t i = 0; i < sizeof(layout_errors) / sizeof(layout_errors[0]); ++i) {
        memcpy(expected, trailer, sizeof(expected));
        expected[layout_errors[i]] ^= 1;
        REJECT_READ(atl_xnu_boot_read_trailer(&loc, expected, sizeof(expected), &decoded), ATL_BOOT_AUX_LAYOUT);
    }
    loc.trailer_va += 8;
    REJECT_READ(atl_xnu_boot_read_trailer(&loc, trailer, 256, &decoded), ATL_BOOT_AUX_LAYOUT);
#undef REJECT_READ
    printf("PASS: exact ATLC v1 wire and retained slots, %u transactional rejections\n", rejected);
    free(allocation);
    free(file);
}
