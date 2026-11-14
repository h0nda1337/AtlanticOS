/* SPDX-License-Identifier: MIT - Original retained XNU bootstrap context. */
#include <atlantic/boot_aux.h>

/* Fixed ATLC v1 offsets. Reserved bytes must be zero. */
enum {
    MAGIC = 0, VERSION = 4, SIZE = 8, TREE_BYTES = 12,
    MANAGED_PA = 16, MANAGED_BYTES = 24, VIRTUAL_BASE = 32, TOP = 40,
    IMAGE_PA = 48, IMAGE_BYTES = 56, ENTRY_PA = 64, CONTEXT_PA = 72, CONTEXT_BYTES = 80,
    MPIDR = 88, COUNTER_FREQ = 96, INITIAL_EL = 104, CPU_COUNT = 108,
    IMAGE_SHA = 112, UPSTREAM = 144, PORT_ID = 164, RESERVED = 196
};

struct Span { const void *base; size_t bytes; };

static int span_valid(struct Span s) {
    return s.base && s.bytes && s.bytes <= UINTPTR_MAX - (uintptr_t)s.base;
}

static int overlap(struct Span a, struct Span b) {
    uintptr_t p = (uintptr_t)a.base, q = (uintptr_t)b.base;
    return p <= q ? q - p < a.bytes : p - q < b.bytes;
}

static void copy(uint8_t *dst, const uint8_t *src, size_t bytes) {
    for (size_t i = 0; i < bytes; ++i) dst[i] = src[i];
}

static void zero(uint8_t *dst, size_t bytes) {
    for (size_t i = 0; i < bytes; ++i) dst[i] = 0;
}

static uint64_t load(const uint8_t *p, unsigned bytes) {
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) value |= (uint64_t)p[i] << (i * 8);
    return value;
}

static void store(uint8_t *p, uint64_t v, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) p[i] = (uint8_t)(v >> (i * 8));
}

static int nonzero(const uint8_t *data, size_t bytes) {
    uint8_t bits = 0;
    for (size_t i = 0; i < bytes; ++i) bits |= data[i];
    return bits != 0;
}

static int identity_valid(const struct AtlanticBootIdentity *id) {
    return (id->initial_el == 1 || id->initial_el == 2) &&
        id->cpu_count && id->cpu_count <= ATL_BOOT_MAX_CPUS &&
        id->counter_frequency && id->counter_frequency <= UINT32_MAX &&
        (id->primary_mpidr & ~UINT64_C(0x000000ff00ffffff)) == 0 &&
        nonzero(id->image_sha256, sizeof(id->image_sha256)) &&
        nonzero(id->upstream_commit, sizeof(id->upstream_commit)) &&
        nonzero(id->port_id, sizeof(id->port_id));
}

static int geometry_valid(const struct AtlanticBootLocator *loc,
                          const struct AtlanticRetainedContext *ctx) {
    const uint64_t page = ATL_XNU_BOOT_AUX_PAGE_BYTES;
    const uint64_t block = ATL_XNU_BOOT_BLOCK_BYTES;
    const uint64_t prefix = ATL_XNU_BOOT_LINK_BASE & (block - 1);
    const uint64_t limit = UINT64_C(0x8000000000);
    if (loc->managed_pa >= limit || (loc->managed_pa & (block - 1)) ||
        (loc->managed_bytes & (block - 1)) || loc->managed_bytes < 2 * block ||
        loc->managed_bytes > ATL_MACHO_MAX_BYTES || loc->managed_bytes > limit - loc->managed_pa ||
        loc->virtual_base != ATL_XNU_BOOT_LINK_BASE - prefix ||
        loc->bootstrap_bytes != ((loc->managed_bytes - prefix) & ~(block - 1)) ||
        loc->top_of_data < loc->managed_pa + 4 * page ||
        loc->top_of_data > loc->managed_pa + loc->bootstrap_bytes - page ||
        (loc->top_of_data & (page - 1)) || loc->tree_pa != loc->top_of_data - 2 * page ||
        loc->boot_args_pa != loc->tree_pa + page ||
        loc->trailer_pa != loc->tree_pa - ATL_XNU_BOOT_TRAILER_BYTES ||
        loc->tree_va != loc->virtual_base + (loc->tree_pa - loc->managed_pa) ||
        loc->trailer_va != loc->tree_va - ATL_XNU_BOOT_TRAILER_BYTES ||
        loc->boot_args_va != loc->tree_va + page ||
        loc->tree_bytes < 8 || loc->tree_bytes > ATL_XNU_TREE_MAX_BYTES)
        return 0;
    return ctx->image_pa == loc->managed_pa + prefix &&
        ctx->image_bytes >= page && (ctx->image_bytes & (page - 1)) == 0 &&
        ctx->context_pa >= ctx->image_pa && ctx->context_pa < loc->tree_pa &&
        ctx->image_bytes == ctx->context_pa - ctx->image_pa &&
        ctx->context_bytes == loc->tree_pa - ctx->context_pa &&
        (ctx->context_bytes & (page - 1)) == 0 && ctx->context_bytes <= 65536 &&
        ctx->entry_pa >= ctx->image_pa && ctx->entry_pa < ctx->context_pa &&
        (ctx->entry_pa & 3) == 0;
}

static void encode(uint8_t *p, const struct AtlanticBootLocator *loc,
                   const struct AtlanticRetainedContext *ctx) {
    const struct AtlanticBootIdentity *id = &ctx->identity;
    store(p + MAGIC, 0x434c5441, 4); /* ATLC */
    store(p + VERSION, 1, 4); /* major=1, minor=0 */
    store(p + SIZE, ATL_XNU_BOOT_TRAILER_BYTES, 4);
    store(p + TREE_BYTES, loc->tree_bytes, 4);
    store(p + MANAGED_PA, loc->managed_pa, 8);
    store(p + MANAGED_BYTES, loc->managed_bytes, 8);
    store(p + VIRTUAL_BASE, loc->virtual_base, 8);
    store(p + TOP, loc->top_of_data, 8);
    store(p + IMAGE_PA, ctx->image_pa, 8);
    store(p + IMAGE_BYTES, ctx->image_bytes, 8);
    store(p + ENTRY_PA, ctx->entry_pa, 8);
    store(p + CONTEXT_PA, ctx->context_pa, 8);
    store(p + CONTEXT_BYTES, ctx->context_bytes, 8);
    store(p + MPIDR, id->primary_mpidr, 8);
    store(p + COUNTER_FREQ, id->counter_frequency, 8);
    store(p + INITIAL_EL, id->initial_el, 4);
    store(p + CPU_COUNT, id->cpu_count, 4);
    copy(p + IMAGE_SHA, id->image_sha256, sizeof(id->image_sha256));
    copy(p + UPSTREAM, id->upstream_commit, sizeof(id->upstream_commit));
    copy(p + PORT_ID, id->port_id, sizeof(id->port_id));
}

int atl_xnu_boot_install_aux(const struct AtlanticMappedArena *arena,
                             const struct AtlanticBootPreparation *prep,
                             const void *tree, size_t tree_bytes,
                             const struct AtlanticBootIdentity *id) {
    const struct Span inputs[] = {{arena, sizeof(*arena)}, {prep, sizeof(*prep)},
                                   {tree, tree_bytes}, {id, sizeof(*id)}};
    for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i)
        if (!span_valid(inputs[i])) return ATL_BOOT_AUX_ARGUMENT;
    if ((uintptr_t)arena % _Alignof(struct AtlanticMappedArena) ||
        (uintptr_t)prep % _Alignof(struct AtlanticBootPreparation) ||
        (uintptr_t)id % _Alignof(struct AtlanticBootIdentity)) return ATL_BOOT_AUX_ARGUMENT;

    const struct Span destination = {arena->base, arena->bytes};
    if (!span_valid(destination)) return ATL_BOOT_AUX_ARGUMENT;
    for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i)
        if (overlap(destination, inputs[i])) return ATL_BOOT_AUX_ALIAS;

    struct AtlanticBootLocator loc;
    int status = atl_xnu_boot_locate(prep->boot_args, sizeof(prep->boot_args), &loc);
    if (status) return status;
    struct AtlanticRetainedContext context;
    context.image_pa = prep->image_pa;
    context.image_bytes = prep->image_bytes;
    context.entry_pa = prep->entry_pa;
    context.context_pa = prep->context_pa;
    context.context_bytes = prep->context_bytes;
    copy((uint8_t *)&context.identity, (const uint8_t *)id, sizeof(context.identity));
    if (!identity_valid(id) || !geometry_valid(&loc, &context) ||
        arena->physical_base != loc.managed_pa || arena->bytes != loc.managed_bytes ||
        prep->managed_pa != loc.managed_pa || prep->managed_bytes != loc.managed_bytes ||
        prep->virtual_base != loc.virtual_base || prep->bootstrap_bytes != loc.bootstrap_bytes ||
        prep->bootstrap_pa != loc.managed_pa || prep->bootstrap_va != loc.virtual_base ||
        prep->top_of_data != loc.top_of_data || prep->tree_pa != loc.tree_pa ||
        prep->tree_va != loc.tree_va || prep->boot_args_pa != loc.boot_args_pa ||
        prep->tree_bytes != loc.tree_bytes || tree_bytes != loc.tree_bytes ||
        prep->image_va != ATL_XNU_BOOT_LINK_BASE ||
        prep->entry_va != loc.virtual_base + (context.entry_pa - loc.managed_pa))
        return ATL_BOOT_AUX_LAYOUT;

    uint8_t *ram = arena->base;
    zero(ram + (context.context_pa - loc.managed_pa),
         (size_t)(loc.top_of_data - context.context_pa));
    encode(ram + (loc.trailer_pa - loc.managed_pa), &loc, &context);
    copy(ram + (loc.tree_pa - loc.managed_pa), tree, tree_bytes);
    copy(ram + (loc.boot_args_pa - loc.managed_pa), prep->boot_args, sizeof(prep->boot_args));
    return 0;
}

int atl_xnu_boot_read_trailer(const struct AtlanticBootLocator *loc,
                              const void *trailer, size_t trailer_bytes,
                              struct AtlanticRetainedContext *out) {
    struct Span wire = {trailer, trailer_bytes};
    const struct Span source = {loc, sizeof(*loc)}, dest = {out, sizeof(*out)};
    if (trailer_bytes != ATL_XNU_BOOT_TRAILER_BYTES || !span_valid(wire) ||
        !span_valid(source) || !span_valid(dest) ||
        (uintptr_t)loc % _Alignof(struct AtlanticBootLocator) ||
        (uintptr_t)out % _Alignof(struct AtlanticRetainedContext)) return ATL_BOOT_AUX_ARGUMENT;
    if (overlap(wire, dest) || overlap(source, dest)) return ATL_BOOT_AUX_ALIAS;

    const uint8_t *p = trailer;
    if (load(p + MAGIC, 4) != 0x434c5441 || load(p + VERSION, 4) != 1 ||
        load(p + SIZE, 4) != ATL_XNU_BOOT_TRAILER_BYTES ||
        load(p + TREE_BYTES, 4) != loc->tree_bytes ||
        load(p + MANAGED_PA, 8) != loc->managed_pa ||
        load(p + MANAGED_BYTES, 8) != loc->managed_bytes ||
        load(p + VIRTUAL_BASE, 8) != loc->virtual_base ||
        load(p + TOP, 8) != loc->top_of_data ||
        nonzero(p + RESERVED, ATL_XNU_BOOT_TRAILER_BYTES - RESERVED))
        return ATL_BOOT_AUX_WIRE;

    struct AtlanticRetainedContext context;
    zero((uint8_t *)&context, sizeof(context));
    context.image_pa = load(p + IMAGE_PA, 8);
    context.image_bytes = load(p + IMAGE_BYTES, 8);
    context.entry_pa = load(p + ENTRY_PA, 8);
    context.context_pa = load(p + CONTEXT_PA, 8);
    context.context_bytes = load(p + CONTEXT_BYTES, 8);
    struct AtlanticBootIdentity *id = &context.identity;
    id->primary_mpidr = load(p + MPIDR, 8);
    id->counter_frequency = load(p + COUNTER_FREQ, 8);
    id->initial_el = (uint32_t)load(p + INITIAL_EL, 4);
    id->cpu_count = (uint32_t)load(p + CPU_COUNT, 4);
    copy(id->image_sha256, p + IMAGE_SHA, sizeof(id->image_sha256));
    copy(id->upstream_commit, p + UPSTREAM, sizeof(id->upstream_commit));
    copy(id->port_id, p + PORT_ID, sizeof(id->port_id));
    if (!geometry_valid(loc, &context) || !identity_valid(id)) return ATL_BOOT_AUX_LAYOUT;
    copy((uint8_t *)out, (const uint8_t *)&context, sizeof(context));
    return 0;
}
