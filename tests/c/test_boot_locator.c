/* SPDX-License-Identifier: MIT - AtlanticOS host locator tests. */
#include <atlantic/boot_locator.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint64_t virtual_base = UINT64_C(0xfffffff007000000);
static uint8_t native_record[ATL_XNU_BOOT_RECORD_BYTES];
static uint8_t record[ATL_XNU_BOOT_RECORD_BYTES];
static unsigned accepted, rejected;

static void put_le(size_t offset, unsigned width, uint64_t value)
{
    for (unsigned i = 0; i < width; ++i)
        record[offset + i] = (uint8_t)(value >> (8 * i));
}

static void set_geometry(uint64_t base, uint64_t bytes, uint64_t top)
{
    put_le(16, 8, base);
    put_le(24, 8, bytes);
    put_le(32, 8, base + top);
    put_le(96, 8, virtual_base + top - 0x8000);
}

static struct AtlanticBootLocator check(const void *bytes, size_t size, int expected)
{
    struct {
        uint64_t before;
        struct AtlanticBootLocator location;
        uint64_t after;
    } guarded;
    uint8_t location_before[sizeof(guarded.location)];
    uint8_t input_before[sizeof(record)];

    memset(&guarded, 0xa5, sizeof(guarded));
    memcpy(location_before, &guarded.location, sizeof(location_before));
    memcpy(input_before, record, sizeof(input_before));
    assert(atl_xnu_boot_locate(bytes, size, &guarded.location) == expected);
    assert(guarded.before == UINT64_C(0xa5a5a5a5a5a5a5a5));
    assert(guarded.after == guarded.before);
    assert(!memcmp(record, input_before, sizeof(record)));
    if (expected) {
        assert(!memcmp(&guarded.location, location_before, sizeof(location_before)));
        ++rejected;
    } else {
        ++accepted;
    }
    return guarded.location;
}

static void reject_field(size_t offset, unsigned width, uint64_t value, int expected)
{
    memcpy(record, native_record, sizeof(record));
    put_le(offset, width, value);
    check(record, sizeof(record), expected);
}

static void check_addresses(const struct AtlanticBootLocator *loc, uint64_t base,
                            uint64_t bytes, uint64_t top, uint32_t tree_bytes)
{
    assert(loc->managed_pa == base && loc->managed_bytes == bytes);
    assert(loc->virtual_base == virtual_base && loc->top_of_data == base + top);
    assert(loc->bootstrap_bytes == bytes - 0x200000);
    assert(loc->tree_pa == base + top - 0x8000 && loc->tree_va == virtual_base + top - 0x8000);
    assert(loc->trailer_pa == loc->tree_pa - 256 && loc->trailer_va == loc->tree_va - 256);
    assert(!(loc->trailer_pa & 7) && !(loc->trailer_va & 7));
    assert(loc->boot_args_pa == base + top - 0x4000 && loc->boot_args_va == virtual_base + top - 0x4000);
    assert(loc->tree_bytes == tree_bytes);
}

int main(int argc, char **argv)
{
    struct AtlanticBootLocator loc;
    uint8_t unaligned[ATL_XNU_BOOT_RECORD_BYTES + 1];
    _Alignas(struct AtlanticBootLocator) uint8_t unaligned_out[sizeof(loc) + 1];
    uint8_t unaligned_before[sizeof(unaligned_out)];
    union {
        uint8_t bytes[ATL_XNU_BOOT_RECORD_BYTES];
        struct AtlanticBootLocator location;
    } alias;
    FILE *fixture;

    assert(argc == 2);
    fixture = fopen(argv[1], "rb");
    assert(fixture && fread(native_record, 1, sizeof(native_record), fixture) == sizeof(native_record));
    assert(fgetc(fixture) == EOF && !fclose(fixture));
    memcpy(record, native_record, sizeof(record));
    loc = check(record, sizeof(record), 0);
    check_addresses(&loc, 0x42000000, 0x2000000, 0x24000, 504);

    memcpy(unaligned + 1, record, sizeof(record));
    loc = check(unaligned + 1, sizeof(record), 0);
    check_addresses(&loc, 0x42000000, 0x2000000, 0x24000, 504);
    assert(!memcmp(unaligned + 1, record, sizeof(record)));

    set_geometry(0x42000000, 0x400000, 0x14000);
    put_le(104, 4, 8);
    loc = check(record, sizeof(record), 0);
    check_addresses(&loc, 0x42000000, 0x400000, 0x14000, 8);

    set_geometry(UINT64_C(0x7ff0000000), 0x10000000, 0xfdfc000);
    put_le(104, 4, 16384);
    loc = check(record, sizeof(record), 0);
    check_addresses(&loc, UINT64_C(0x7ff0000000), 0x10000000, 0xfdfc000, 16384);

    check(NULL, sizeof(record), ATL_BOOT_LOCATOR_ARGUMENT);
    check(record, sizeof(record) - 1, ATL_BOOT_LOCATOR_ARGUMENT);
    check(record, sizeof(record) + 1, ATL_BOOT_LOCATOR_ARGUMENT);
    assert(atl_xnu_boot_locate(record, sizeof(record), NULL) == ATL_BOOT_LOCATOR_ARGUMENT);
    ++rejected;

    memset(unaligned_out, 0xa5, sizeof(unaligned_out));
    memcpy(unaligned_before, unaligned_out, sizeof(unaligned_before));
    assert(atl_xnu_boot_locate(record, sizeof(record),
           (struct AtlanticBootLocator *)(uintptr_t)(unaligned_out + 1)) ==
           ATL_BOOT_LOCATOR_ARGUMENT);
    assert(!memcmp(unaligned_out, unaligned_before, sizeof(unaligned_before)));
    ++rejected;

    memcpy(alias.bytes, native_record, sizeof(alias.bytes));
    assert(atl_xnu_boot_locate(alias.bytes, sizeof(alias.bytes), &alias.location) ==
           ATL_BOOT_LOCATOR_ARGUMENT);
    assert(!memcmp(alias.bytes, native_record, sizeof(alias.bytes)));
    ++rejected;

    reject_field(0, 2, 1, ATL_BOOT_LOCATOR_ABI);
    reject_field(2, 2, 1, ATL_BOOT_LOCATOR_ABI);
    reject_field(88, 4, 0x41544c32, ATL_BOOT_LOCATOR_ABI);
    reject_field(8, 8, virtual_base + 0x4000, ATL_BOOT_LOCATOR_RANGE);
    reject_field(8, 8, 0x7000000, ATL_BOOT_LOCATOR_RANGE);
    reject_field(16, 8, 0x42000001, ATL_BOOT_LOCATOR_RANGE);
    reject_field(16, 8, UINT64_MAX - 0x1fffff, ATL_BOOT_LOCATOR_RANGE);
    reject_field(16, 8, UINT64_C(0x7ffe200000), ATL_BOOT_LOCATOR_RANGE);
    reject_field(16, 8, UINT64_C(0x8000000000), ATL_BOOT_LOCATOR_RANGE);
    reject_field(24, 8, 0x2000001, ATL_BOOT_LOCATOR_RANGE);
    reject_field(24, 8, 0, ATL_BOOT_LOCATOR_RANGE);
    reject_field(24, 8, 0x200000, ATL_BOOT_LOCATOR_RANGE);
    reject_field(24, 8, 0x10200000, ATL_BOOT_LOCATOR_RANGE);
    reject_field(24, 8, UINT64_MAX - 0x1fffff, ATL_BOOT_LOCATOR_RANGE);
    reject_field(32, 8, 0x41ffc000, ATL_BOOT_LOCATOR_RANGE);
    reject_field(32, 8, 0x42024001, ATL_BOOT_LOCATOR_RANGE);
    reject_field(32, 8, 0x42010000, ATL_BOOT_LOCATOR_RANGE);
    reject_field(32, 8, 0x43e00000, ATL_BOOT_LOCATOR_RANGE);
    reject_field(32, 8, 0x43e04000, ATL_BOOT_LOCATOR_RANGE);
    reject_field(32, 8, 0x44000000, ATL_BOOT_LOCATOR_RANGE);
    reject_field(32, 8, UINT64_MAX - 0x3fff, ATL_BOOT_LOCATOR_RANGE);
    reject_field(96, 8, 0x4201c000, ATL_BOOT_LOCATOR_TREE);
    reject_field(96, 8, virtual_base + 0x20000, ATL_BOOT_LOCATOR_TREE);
    reject_field(96, 8, virtual_base + 0x1c008, ATL_BOOT_LOCATOR_TREE);
    reject_field(104, 4, 0, ATL_BOOT_LOCATOR_TREE);
    reject_field(104, 4, 7, ATL_BOOT_LOCATOR_TREE);
    reject_field(104, 4, 16385, ATL_BOOT_LOCATOR_TREE);
    reject_field(104, 4, UINT32_MAX, ATL_BOOT_LOCATOR_TREE);

    printf("PASS {\"accepted\":%u,\"rejected\":%u}\n", accepted, rejected);
    return 0;
}
