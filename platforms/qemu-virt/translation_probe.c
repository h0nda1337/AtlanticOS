/* SPDX-License-Identifier: MIT - original QEMU EL1 translation probe. */
#include <atlantic/boot_aux.h>
#include <atlantic/entry.h>

#if !ATL_TEST_TRANSLATION
#error Compile only for the QEMU translation probe
#endif

#include "translation_probe.h"

#define PAGE UINT64_C(4096)
#define PA_MASK UINT64_C(0x0000fffffffff000)
#define TCR_BOOT UINT64_C(0x22a5192519)
#define MAIR_LAB UINT64_C(0x04ff) /* Attr0 WBWA normal, Attr1 Device-nGnRE. */
#define DESCRIPTOR UINT64_C(3)
#define DESC_ACCESS (UINT64_C(1) << 10)
#define DESC_INNER (UINT64_C(3) << 8)
#define DESC_RO (UINT64_C(1) << 7)
#define DESC_DEVICE (UINT64_C(1) << 2)
#define DESC_PXN (UINT64_C(1) << 53)
#define DESC_UXN (UINT64_C(1) << 54)
#define PAGES_PER_SET 16u

static const uint64_t managed_base = 0x42000000;
static const uint64_t managed_bytes = 0x2000000;
static const uint64_t uart = 0x09000000;
static const uint64_t fixture_helper_offset = 0x28;
static const uint64_t fixture_helper_bytes = UINT64_C(0xd65f03c0d2824683);

extern uint8_t __rx_start[], __rx_end[], __ro_start[], __ro_end[];
extern uint8_t __rw_start[], __image_memory_end[];
extern const uint8_t atl_probe_macho_start[], atl_probe_macho_end[];
extern const uint8_t atl_probe_macho_hash[];
extern const uint8_t atl_translation_return[], atl_translation_return_end[];
extern const uint8_t atl_translation_vectors[];
extern uint64_t atl_translation_try_store(uint64_t va);
extern uint64_t atl_translation_try_execute(uint64_t va);

typedef uint64_t PageTable[512];
struct TableSet {
    _Alignas(4096) PageTable pages[PAGES_PER_SET];
    unsigned used;
};

static struct TableSet first, second;
static uint8_t native_args_copy[ATL_XNU_BOOT_RECORD_BYTES];
static uint8_t atlc_copy[ATL_XNU_BOOT_TRAILER_BYTES];

static void log(const char *message)
{
    (void)atlantic_early_platform.write(message);
}

static int fail(const char *stage)
{
    log("FAIL:TRANSLATION_");
    log(stage);
    log("\n");
    return 1;
}

static void log_hex(const char *key, uint64_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    char hex[] = "0x0000000000000000\n";
    for (unsigned i = 0; i < 16; ++i)
        hex[2 + i] = digits[(value >> (60 - 4 * i)) & 15];
    log(key);
    log(hex);
}

static uint64_t addr(const void *p)
{
    return (uintptr_t)p;  /* Only lab-owned identity mappings use C addresses. */
}

static int map_page(struct TableSet *set, unsigned root, uint64_t va,
                    uint64_t pa, uint64_t permissions)
{
    PageTable *table = &set->pages[root];
    for (unsigned shift = 30; shift >= 21; shift -= 9) {
        uint64_t *descriptor = &(*table)[(va >> shift) & 511];
        if (!*descriptor) {
            if (set->used == PAGES_PER_SET)
                return 1;
            *descriptor = addr(&set->pages[set->used++]) | DESCRIPTOR;
        }
        table = (PageTable *)(uintptr_t)(*descriptor & PA_MASK);
    }
    uint64_t *leaf = &(*table)[(va >> 12) & 511];
    if (*leaf)
        return 1;
    *leaf = pa | DESCRIPTOR | DESC_ACCESS | DESC_UXN | permissions;
    return 0;
}

static int map_range(struct TableSet *set, unsigned root, uint64_t va,
                     uint64_t pa, uint64_t bytes, uint64_t permissions)
{
    if ((va | pa | bytes) & (PAGE - 1))
        return 1;
    for (uint64_t i = 0; i < bytes; i += PAGE) {
        if (map_page(set, root, va + i, pa + i, permissions))
            return 1;
    }
    return 0;
}

static int build_tables(struct TableSet *tables, const struct AtlanticMachoPlan *plan,
                        const struct AtlanticBootPreparation *boot, int managed_identity)
{
    tables->used = 2; /* Root 0 and root 1 already zeroed by original entry. */
    const uint64_t rw = DESC_INNER | DESC_PXN;
    if (map_range(tables, 0, addr(__rx_start), addr(__rx_start),
                  addr(__rx_end) - addr(__rx_start), DESC_INNER | DESC_RO) ||
        map_range(tables, 0, addr(__ro_start), addr(__ro_start),
                  addr(__ro_end) - addr(__ro_start), rw | DESC_RO) ||
        map_range(tables, 0, addr(__rw_start), addr(__rw_start),
                  addr(__image_memory_end) - addr(__rw_start), rw) ||
        map_page(tables, 0, uart, uart, DESC_DEVICE | DESC_PXN))
        return 1;

    for (uint32_t i = 0; i < plan->segment_count; ++i) {
        const struct AtlanticMachoSegment *s = &plan->segments[i];
        uint64_t pa = boot->image_pa + (s->virtual_address - boot->image_va);
        uint64_t prot = DESC_INNER;
        if (!(s->protection & 2))
            prot |= DESC_RO;
        if (!(s->protection & 4))
            prot |= DESC_PXN;
        if (map_range(tables, 1, s->virtual_address, pa, s->mapping_bytes, prot) ||
            (managed_identity && map_range(tables, 0, pa, pa, s->mapping_bytes, prot)))
            return 1;
    }
    uint64_t tail_bytes = boot->top_of_data - boot->context_pa;
    uint64_t tail_va = boot->virtual_base + (boot->context_pa - boot->managed_pa);
    return map_range(tables, 1, tail_va, boot->context_pa, tail_bytes, rw) ||
           (managed_identity && map_range(tables, 0, boot->context_pa, boot->context_pa,
                                          tail_bytes, rw));
}

static void enable_translation(const struct TableSet *tables)
{
    uint64_t sctlr;
    __asm__ volatile(
        "dsb sy; msr vbar_el1, %4; msr mair_el1, %3; msr tcr_el1, %2; isb;"
        "msr ttbr0_el1, %0; msr ttbr1_el1, %1; isb; tlbi vmalle1; dsb sy; isb"
        : : "r"(addr(&tables->pages[0])), "r"(addr(&tables->pages[1])),
            "r"(TCR_BOOT), "r"(MAIR_LAB), "r"(addr(atl_translation_vectors)) : "memory");
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    /* WXN forces no execution from writable pages. C/I remain off for the lab. */
    sctlr |= (UINT64_C(1) << 19) | 1;
    __asm__ volatile("msr sctlr_el1, %0; isb" : : "r"(sctlr) : "memory");
}

static void replace_translation(const struct TableSet *tables)
{
    __asm__ volatile(
        "dsb sy; msr ttbr0_el1, %0; isb; msr ttbr1_el1, %1; isb;"
        "tlbi vmalle1; dsb sy; isb"
        : : "r"(addr(&tables->pages[0])), "r"(addr(&tables->pages[1])) : "memory");
}

static int using_tables(const struct TableSet *tables)
{
    uint64_t ttbr0, ttbr1, tcr, sctlr;
    const uint64_t mmu_and_wxn = (UINT64_C(1) << 19) | 1;
    __asm__ volatile("mrs %0, ttbr0_el1; mrs %1, ttbr1_el1; mrs %2, tcr_el1; mrs %3, sctlr_el1"
                     : "=r"(ttbr0), "=r"(ttbr1), "=r"(tcr), "=r"(sctlr));
    return ttbr0 == addr(&tables->pages[0]) && ttbr1 == addr(&tables->pages[1]) &&
           tcr == TCR_BOOT && (sctlr & (mmu_and_wxn | 0x1004)) == mmu_and_wxn;
}

static uint64_t par_read(uint64_t va)
{
    uint64_t result;
    __asm__ volatile("at s1e1r, %1; isb; mrs %0, par_el1"
                     : "=r"(result) : "r"(va) : "memory");
    return result;
}

static uint64_t par_write(uint64_t va)
{
    uint64_t result;
    __asm__ volatile("at s1e1w, %1; isb; mrs %0, par_el1"
                     : "=r"(result) : "r"(va) : "memory");
    return result;
}

static int prove_mapping(uint64_t va, uint64_t pa, int writable)
{
    uint64_t read = par_read(va), write = par_write(va);
    return !(read & 1) && (read & PA_MASK) == (pa & PA_MASK) &&
           (read >> 56) == 0xff &&
           (writable ? !(write & 1) && (write & PA_MASK) == (pa & PA_MASK) :
                       (write & 0x7f) == 0x1f);
}

static int prove_descriptors(uint64_t va, uint64_t pa, int writable, int executable)
{
    uint64_t ttbr;
    if (va >> 63)
        __asm__ volatile("mrs %0, ttbr1_el1" : "=r"(ttbr));
    else
        __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(ttbr));
    uint64_t descriptor = ttbr | DESCRIPTOR;
    for (unsigned shift = 30; shift >= 12; shift -= 9) {
        if ((descriptor & 3) != DESCRIPTOR)
            return 0;
        const volatile uint64_t *table = (const volatile uint64_t *)(uintptr_t)(descriptor & PA_MASK);
        descriptor = table[(va >> shift) & 511];
    }
    const uint64_t permissions = (writable ? 0 : DESC_RO) | (executable ? 0 : DESC_PXN) |
        DESC_INNER | DESC_UXN | DESC_ACCESS | DESCRIPTOR;
    return descriptor == ((pa & PA_MASK) | permissions);
}

static int prove_fixture(const struct AtlanticMachoPlan *image, int identity)
{
    for (uint32_t i = 0; i < image->segment_count; ++i) {
        const struct AtlanticMachoSegment *segment = &image->segments[i];
        uint64_t offset = segment->virtual_address - image->virtual_base + 0x4000;
        const volatile uint8_t *bytes = (const volatile uint8_t *)(uintptr_t)segment->virtual_address;
        for (uint64_t page = 0; page < segment->mapping_bytes; page += PAGE) {
            uint64_t pa = managed_base + offset + page;
            uint64_t va = segment->virtual_address + page;
            int writable = !!(segment->protection & 2);
            int executable = !!(segment->protection & 4);
            if (!prove_mapping(va, pa, writable) || !prove_descriptors(va, pa, writable, executable))
                return 0;
            if (identity ? !prove_mapping(pa, pa, writable) ||
                           !prove_descriptors(pa, pa, writable, executable) : !(par_read(pa) & 1))
                return 0;
        }
        for (uint64_t j = 0; j < segment->mapping_bytes; ++j) {
            uint8_t expected = j < segment->file_bytes ?
                atl_probe_macho_start[segment->file_offset + j] : 0;
            if (bytes[j] != expected)
                return 0;
        }
    }
    return 1;
}

static int prove_rw(uint64_t va, uint64_t pa, int identity)
{
    volatile uint8_t *high = (volatile uint8_t *)(uintptr_t)va;
    volatile uint8_t *low = (volatile uint8_t *)(uintptr_t)pa;
    uint8_t initial = *high;
    *high = initial ^ 0x96;
    if (*high != (initial ^ 0x96) || (identity && *low != (initial ^ 0x96)))
        return 0;
    if (identity)
        *low = initial;
    else
        *high = initial;
    return *high == initial;
}

static void write_va(uint64_t va, const uint8_t *bytes, size_t length)
{
    volatile uint8_t *to = (volatile uint8_t *)(uintptr_t)va;
    for (size_t i = 0; i < length; ++i)
        to[i] = bytes[i];
}

static int matches_va(uint64_t va, const uint8_t *bytes, size_t length)
{
    const volatile uint8_t *from = (const volatile uint8_t *)(uintptr_t)va;
    for (size_t i = 0; i < length; ++i) {
        if (from[i] != bytes[i])
            return 0;
    }
    return 1;
}

static uint64_t prove_nx(uint64_t va)
{
    static const uint8_t zeroes[8];
    if (!matches_va(va, zeroes, sizeof(zeroes)))
        return 0;
    write_va(va, atl_translation_return, sizeof(zeroes));
    __asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
    uint64_t esr = atl_translation_try_execute(va);
    write_va(va, zeroes, sizeof(zeroes));
    return esr;
}

static int equal(const uint8_t *a, const uint8_t *b, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        if (a[i] != b[i])
            return 0;
    }
    return 1;
}

static int zero_va(uint64_t va, size_t length)
{
    const volatile uint8_t *from = (const volatile uint8_t *)(uintptr_t)va;
    for (size_t i = 0; i < length; ++i) {
        if (from[i])
            return 0;
    }
    return 1;
}

static int prove_aux(const struct AtlanticBootLocator *where,
                     const struct AtlanticBootIdentity *observed,
                     const struct AtlanticBootPreparation *boot, const uint8_t *tree, int identity)
{
    struct AtlanticRetainedContext context;
    uint64_t context_va = where->virtual_base + (boot->context_pa - where->managed_pa);
    for (uint64_t pa = boot->context_pa; pa < where->top_of_data; pa += PAGE) {
        uint64_t va = context_va + (pa - boot->context_pa);
        if (!prove_mapping(va, pa, 1) || !prove_descriptors(va, pa, 1, 0))
            return 0;
        if (identity ? !prove_mapping(pa, pa, 1) ||
                       !prove_descriptors(pa, pa, 1, 0) : !(par_read(pa) & 1))
            return 0;
    }
    if (atl_xnu_boot_read_trailer(where, (const void *)(uintptr_t)where->trailer_va,
                                  ATL_XNU_BOOT_TRAILER_BYTES, &context))
        return 0;
    const struct AtlanticBootIdentity *id = &context.identity;
    return context.image_pa == boot->image_pa && context.image_bytes == boot->image_bytes &&
           context.entry_pa == boot->entry_pa && context.context_pa == boot->context_pa &&
           context.context_bytes == boot->context_bytes &&
           id->primary_mpidr == observed->primary_mpidr &&
           id->counter_frequency == observed->counter_frequency &&
           id->initial_el == observed->initial_el && id->cpu_count == observed->cpu_count &&
           equal(id->image_sha256, observed->image_sha256, sizeof(id->image_sha256)) &&
           equal(id->upstream_commit, observed->upstream_commit, sizeof(id->upstream_commit)) &&
           equal(id->port_id, observed->port_id, sizeof(id->port_id)) &&
           zero_va(context_va, (size_t)(where->trailer_va - context_va)) &&
           matches_va(where->tree_va, tree, where->tree_bytes) &&
           zero_va(where->tree_va + where->tree_bytes, ATL_XNU_BOOT_AUX_PAGE_BYTES - where->tree_bytes) &&
           matches_va(where->boot_args_va, native_args_copy, sizeof(native_args_copy)) &&
           zero_va(where->boot_args_va + sizeof(native_args_copy),
                   ATL_XNU_BOOT_AUX_PAGE_BYTES - sizeof(native_args_copy));
}

int atl_translation_probe(const struct AtlanticBootInventory *inventory,
                          const struct AtlanticEntryCapture *capture,
                          const uint8_t *tree, uint32_t tree_bytes)
{
    struct AtlanticMappedArena arena = {(void *)(uintptr_t)managed_base, managed_base, managed_bytes};
    struct AtlanticBootRequest request = {managed_base, managed_bytes, tree_bytes, 4096, "-v", 2};
    struct AtlanticRetainedImage source = {atl_probe_macho_start, addr(atl_probe_macho_start),
                                           addr(atl_probe_macho_end) - addr(atl_probe_macho_start)};
    struct AtlanticBootPreparation boot;
    struct AtlanticBootLocator where, copied_where;
    struct AtlanticMachoPlan image;
    struct AtlanticBootIdentity identity;
    static const uint8_t simulated_commit[20] = "QEMU-FIXTURE-ID";
    static const uint8_t simulated_port[32] = "QEMU-FIXTURE-NO-XNU-PORT-ID";
    identity.primary_mpidr = capture->mpidr & UINT64_C(0x000000ff00ffffff);
    identity.counter_frequency = capture->cntfrq;
    identity.initial_el = (uint32_t)(capture->current_el >> 2);
    identity.cpu_count = inventory->cpu_count;
    for (size_t i = 0; i < sizeof(identity.image_sha256); ++i)
        identity.image_sha256[i] = atl_probe_macho_hash[i];
    for (size_t i = 0; i < sizeof(identity.upstream_commit); ++i)
        identity.upstream_commit[i] = simulated_commit[i];
    for (size_t i = 0; i < sizeof(identity.port_id); ++i)
        identity.port_id[i] = simulated_port[i];
    if (atl_macho_plan(source.base, source.bytes, &image) ||
        image.segment_count != 5 || image.virtual_span != 0x18000 ||
        image.virtual_base != UINT64_C(0xfffffff007004000) ||
        image.entry != UINT64_C(0xfffffff007014000) ||
        atl_xnu_image_materialize(inventory, &source, &request, &arena, &boot) ||
        atl_xnu_boot_install_aux(&arena, &boot, tree, tree_bytes, &identity) ||
        atl_xnu_boot_locate(boot.boot_args, sizeof(boot.boot_args), &where))
        return fail("PREPARATION");

    if (addr(atl_translation_return_end) - addr(atl_translation_return) != 8)
        return fail("RETURN_FIXTURE");
    /* Use ordinary fixture RW and NX data, outside the installed context. */
    const uint64_t nx_va = UINT64_C(0xfffffff00700c080);
    if (build_tables(&first, &image, &boot, 1) || build_tables(&second, &image, &boot, 0))
        return fail("TABLE_CAPACITY");

    enable_translation(&first);
    if (!using_tables(&first) || !prove_mapping(where.boot_args_va, where.boot_args_pa, 1))
        return fail("FIRST_NATIVE_ARGS_MAPPING");
    for (size_t i = 0; i < sizeof(native_args_copy); ++i)
        native_args_copy[i] = ((const volatile uint8_t *)(uintptr_t)where.boot_args_va)[i];
    uint64_t uart_par = par_read(uart);
    if (!matches_va(where.boot_args_va, boot.boot_args, sizeof(boot.boot_args)) ||
        (uart_par & 1) || (uart_par & PA_MASK) != uart || (uart_par >> 56) != 0x04 ||
        !prove_fixture(&image, 1) ||
        !prove_aux(&where, &identity, &boot, tree, 1))
        return fail("FIRST_MAP_READBACK");
    for (size_t i = 0; i < sizeof(atlc_copy); ++i)
        atlc_copy[i] = ((const volatile uint8_t *)(uintptr_t)where.trailer_va)[i];
    log("PASS:TRANSLATION_ATLC_FIRST_MAP\n");
    const uint64_t exec_va = boot.entry_va + fixture_helper_offset;
    const uint64_t data_va = UINT64_C(0xfffffff00700c030);
    const uint64_t data_pa = managed_base + (data_va - boot.virtual_base);
    if (*(const volatile uint64_t *)(uintptr_t)exec_va != fixture_helper_bytes)
        return fail("RX_HELPER_BYTES");
    register uint64_t addr_arg __asm__("x9") = exec_va;
    register uint64_t helper_value __asm__("x3");
    __asm__ volatile("blr x9" : "=r"(helper_value) : "r"(addr_arg) : "x30", "memory");
    uint64_t rx_store = atl_translation_try_store(exec_va);
    uint64_t head_store = atl_translation_try_store(boot.image_va);
    uint64_t nx_exec = prove_nx(nx_va);
    if (helper_value != 0x1234 || rx_store != UINT64_C(0x9600004f) ||
        head_store != UINT64_C(0x9600004f) || nx_exec != UINT64_C(0x8600000f) ||
        !prove_rw(data_va, data_pa, 1))
        return fail("HARDWARE_PERMISSIONS");
    log_hex("RX_STORE_ESR=", rx_store);
    log_hex("HEADER_STORE_ESR=", head_store);
    log_hex("RW_DATA_EXEC_ESR=", nx_exec);
    log("PASS:TRANSLATION_HIGH_RX_EXEC_RO_STORE_FAULT_RW_NX_EXEC_FAULT\n");

    replace_translation(&second);
    if (!using_tables(&second) ||
        atl_xnu_boot_locate(native_args_copy, sizeof(native_args_copy), &copied_where) ||
        copied_where.trailer_va != where.trailer_va ||
        copied_where.tree_va != where.tree_va ||
        !prove_fixture(&image, 0) || !prove_aux(&copied_where, &identity, &boot, tree, 0) ||
        !matches_va(copied_where.trailer_va, atlc_copy, sizeof(atlc_copy)) ||
        !prove_rw(data_va, data_pa, 0) ||
        !(par_read(boot.context_pa) & 1) || !(par_read(managed_base) & 1) ||
        atl_translation_try_store(exec_va) != UINT64_C(0x9600004f) ||
        atl_translation_try_store(boot.image_va) != UINT64_C(0x9600004f) ||
        prove_nx(nx_va) != UINT64_C(0x8600000f))
        return fail("REPLACEMENT_MAP");
    log("PASS:TRANSLATION_ATLC_REPLACEMENT_MAP\n");
    log_hex("TRANSLATION_IMAGE_PA=", boot.image_pa);
    log_hex("TRANSLATION_ENTRY_VA=", boot.entry_va);
    log_hex("TRANSLATION_TRAILER_VA=", copied_where.trailer_va);
    log_hex("TRANSLATION_TREE_VA=", copied_where.tree_va);
    log_hex("TRANSLATION_BOOT_ARGS_VA=", copied_where.boot_args_va);
    log("PASS:TRANSLATION_REPLACED_ROOTS_RETAINED_HIGH_BYTES_LOW_ALIAS_REMOVED\n"
        "FIXTURE_ENTRY_NOT_EXECUTED\n");
    return 0;
}
