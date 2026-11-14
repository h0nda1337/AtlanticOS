/* SPDX-License-Identifier: MIT - Original static-image byte materialization. */
#include <atlantic/image_materialize.h>

struct Span {
    const void *base;
    size_t bytes;
};

struct SegmentCopy {
    size_t destination, mapping_bytes, source, file_bytes;
};

static int valid_span(struct Span span) {
    return !span.bytes || (span.base && span.bytes<=UINTPTR_MAX-(uintptr_t)span.base);
}

static int overlaps(uint64_t a, uint64_t a_bytes, uint64_t b, uint64_t b_bytes) {
    return a_bytes && b_bytes && (a<=b ? b-a<a_bytes : a-b<b_bytes);
}

static int overlaps_span(struct Span a, struct Span b) {
    return overlaps((uintptr_t)a.base,a.bytes,(uintptr_t)b.base,b.bytes);
}

int atl_xnu_image_materialize(const struct AtlanticBootInventory *inventory,
                              const struct AtlanticRetainedImage *source,
                              const struct AtlanticBootRequest *request,
                              const struct AtlanticMappedArena *arena,
                              struct AtlanticBootPreparation *out) {
    struct AtlanticMachoPlan plan;
    struct AtlanticBootPreparation candidate;
    struct SegmentCopy copies[ATL_MACHO_MAX_SEGMENTS];
    struct Span destination, result={out,sizeof(*out)};
    struct Span inputs[]={ {inventory,sizeof(*inventory)}, {source,sizeof(*source)},
                           {request,sizeof(*request)}, {arena,sizeof(*arena)}, {0,0}, {0,0} };
    uint64_t image_offset, retained_offset;
    int status;
    for (size_t i=0;i<4;++i)
        if (!valid_span(inputs[i])) return ATL_MATERIALIZE_ARGUMENT;
    if (!valid_span(result) ||
        (uintptr_t)inventory%_Alignof(struct AtlanticBootInventory) ||
        (uintptr_t)source%_Alignof(struct AtlanticRetainedImage) ||
        (uintptr_t)request%_Alignof(struct AtlanticBootRequest) ||
        (uintptr_t)arena%_Alignof(struct AtlanticMappedArena) ||
        (uintptr_t)out%_Alignof(struct AtlanticBootPreparation))
        return ATL_MATERIALIZE_ARGUMENT;
    for (size_t i=0;i<4;++i)
        if (overlaps_span(result,inputs[i])) return ATL_MATERIALIZE_ALIAS;
    destination=(struct Span){arena->base,arena->bytes};
    if (!destination.bytes || !valid_span(destination)) return ATL_MATERIALIZE_ARGUMENT;
    if (overlaps_span(destination,result)) return ATL_MATERIALIZE_ALIAS;
    for (size_t i=0;i<4;++i)
        if (overlaps_span(destination,inputs[i])) return ATL_MATERIALIZE_ALIAS;
    inputs[4]=(struct Span){request->command,request->command_bytes};
    inputs[5]=(struct Span){source->base,source->bytes};
    if (!source->bytes) return ATL_MATERIALIZE_ARGUMENT;
    for (size_t i=4;i<sizeof(inputs)/sizeof(inputs[0]);++i) {
        if (!valid_span(inputs[i])) return ATL_MATERIALIZE_ARGUMENT;
        if (overlaps_span(destination,inputs[i]) || overlaps_span(result,inputs[i]))
            return ATL_MATERIALIZE_ALIAS;
    }
    if (arena->physical_base!=request->managed_pa || arena->bytes!=request->managed_bytes)
        return ATL_MATERIALIZE_ARENA;
    status=atl_xnu_boot_prepare(inventory,source->base,source->bytes,request,&candidate);
    if (status) return status;
    if (source->physical_base<candidate.dram_pa || source->bytes>UINT64_MAX-source->physical_base)
        return ATL_MATERIALIZE_SOURCE;
    retained_offset=source->physical_base-candidate.dram_pa;
    if (retained_offset>candidate.dram_bytes || source->bytes>candidate.dram_bytes-retained_offset)
        return ATL_MATERIALIZE_SOURCE;
    if (overlaps(source->physical_base,source->bytes,candidate.managed_pa,candidate.managed_bytes))
        return ATL_MATERIALIZE_ALIAS;

    /* The preparation validates the immutable file, and repeating the parser
     * supplies the exact segment spans without accepting a caller-made plan. */
    status=atl_macho_plan(source->base,source->bytes,&plan);
    if (status) return status;
    if (candidate.image_pa<candidate.managed_pa || candidate.image_va!=plan.virtual_base ||
        candidate.image_bytes!=plan.virtual_span || candidate.entry_va!=plan.entry ||
        !plan.segment_count || plan.segment_count>ATL_MACHO_MAX_SEGMENTS)
        return ATL_MATERIALIZE_LAYOUT;
    image_offset=candidate.image_pa-candidate.managed_pa;
    if (image_offset>arena->bytes || candidate.image_bytes>arena->bytes-image_offset)
        return ATL_MATERIALIZE_LAYOUT;
    for (uint32_t i=0;i<plan.segment_count;++i) {
        const struct AtlanticMachoSegment *segment=&plan.segments[i];
        uint64_t offset;
        if (segment->virtual_address<plan.virtual_base) return ATL_MATERIALIZE_LAYOUT;
        offset=segment->virtual_address-plan.virtual_base;
        if (offset>candidate.image_bytes || segment->mapping_bytes>candidate.image_bytes-offset ||
            segment->file_bytes>segment->mapping_bytes || segment->file_offset>source->bytes ||
            segment->file_bytes>source->bytes-segment->file_offset)
            return ATL_MATERIALIZE_LAYOUT;
        copies[i]=(struct SegmentCopy){(size_t)(image_offset+offset),(size_t)segment->mapping_bytes,
                                        (size_t)segment->file_offset,(size_t)segment->file_bytes};
    }

    /* All rejection paths precede the first write. Inputs stay retained and
     * immutable while byte copy and zero-fill commit the validated spans. */
    for (uint32_t i=0;i<plan.segment_count;++i) {
        const struct SegmentCopy *copy=&copies[i];
        uint8_t *to=(uint8_t *)arena->base+copy->destination;
        const uint8_t *from=(const uint8_t *)source->base+copy->source;
        size_t j;
        for (j=0;j<copy->file_bytes;++j) to[j]=from[j];
        for (;j<copy->mapping_bytes;++j) to[j]=0;
    }
    for (size_t i=0;i<sizeof(candidate);++i)
        ((uint8_t *)out)[i]=((const uint8_t *)&candidate)[i];
    return 0;
}
