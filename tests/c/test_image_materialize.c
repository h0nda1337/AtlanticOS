/* SPDX-License-Identifier: MIT - Host tests for original image byte placement. */
#include <atlantic/image_materialize.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ARENA_BYTES=4*1024*1024, GUARD_BYTES=32, ALLOCATION_BYTES=ARENA_BYTES+2*GUARD_BYTES };
static const uint64_t ARENA_PA=UINT64_C(0x42000000), BANK_PA=UINT64_C(0x40000000);
static const uint64_t BANK_BYTES=UINT64_C(0x8000000), SOURCE_PA=UINT64_C(0x40080000);
static const uint8_t MARK=0xa5;
static unsigned failures, successful;

struct GuardedResult {
    uint64_t before;
    struct AtlanticBootPreparation value;
    uint64_t after;
};

static void repeated(const void *data, size_t bytes, uint8_t expected) {
    const uint8_t *p=data;
    for (size_t i=0;i<bytes;++i) assert(p[i]==expected);
}

static uint8_t *read_file(const char *path, size_t *bytes) {
    FILE *file=fopen(path,"rb");
    assert(file && !fseek(file,0,SEEK_END));
    long end=ftell(file);
    assert(end>0 && end<=ATL_MACHO_MAX_BYTES);
    *bytes=(size_t)end;
    rewind(file);
    uint8_t *data=malloc(*bytes);
    assert(data && fread(data,1,*bytes,file)==*bytes && !fclose(file));
    return data;
}

static void put_le64(uint8_t *data, uint64_t value) {
    for (unsigned i=0;i<8;++i) data[i]=(uint8_t)(value>>(8u*i));
}

static uint32_t get_le32(const uint8_t *data) {
    return data[0]|(uint32_t)data[1]<<8|(uint32_t)data[2]<<16|(uint32_t)data[3]<<24;
}

static void expect_success(uint8_t *allocation, const struct AtlanticBootInventory *inventory,
                           const struct AtlanticRetainedImage *source,
                           const struct AtlanticBootRequest *request,
                           const struct AtlanticMappedArena *arena, size_t gap_pages) {
    const uint8_t *file=source->base;
    uint8_t *saved=malloc(source->bytes);
    assert(saved);
    memcpy(saved,file,source->bytes);
    memset(allocation,MARK,ALLOCATION_BYTES);
    struct GuardedResult result;
    struct AtlanticBootPreparation reference;
    memset(&result,MARK,sizeof(result));
    assert(!atl_xnu_boot_prepare(inventory,source->base,source->bytes,request,&reference));
    assert(!atl_xnu_image_materialize(inventory,source,request,arena,&result.value));
    assert(!memcmp(&result.value,&reference,sizeof(reference)));
    assert(result.value.image_pa==ARENA_PA+0x4000 && result.value.entry_pa==ARENA_PA+0x4498);
    assert(result.value.entry_va==ATL_XNU_BOOT_LINK_BASE+0x498);
    repeated(&result.before,sizeof(result.before),MARK);
    repeated(&result.after,sizeof(result.after),MARK);
    assert(!memcmp(saved,file,source->bytes));
    free(saved);

    /* Independent fixture geometry, established by the retained LLD report.
     * Moving only __LINKEDIT creates holes without moving any file offset. */
    const size_t linkedit=0x14000+gap_pages*ATL_MACHO_PAGE_BYTES;
    const uint8_t *ram=arena->base;
    repeated(allocation,GUARD_BYTES,MARK);
    repeated(ram,0x4000,MARK);
    assert(!memcmp(ram+0x4000,file,0xc000));
    repeated(ram+0x10000,0x4000,0);
    repeated(ram+0x14000,linkedit-0x14000,MARK);
    assert(!memcmp(ram+linkedit,file+0xc000,304));
    repeated(ram+linkedit+304,ATL_MACHO_PAGE_BYTES-304,0);
    const size_t image_end=linkedit+ATL_MACHO_PAGE_BYTES;
    assert(result.value.context_pa==ARENA_PA+image_end);
    /* Covers the context, tree, boot_args destination and rest of managed RAM. */
    repeated(ram+image_end,ARENA_BYTES-image_end,MARK);
    repeated(ram+ARENA_BYTES,GUARD_BYTES,MARK);
    ++successful;
}

struct Trial {
    struct AtlanticBootInventory inventory;
    struct AtlanticRetainedImage source;
    struct AtlanticBootRequest request;
    struct AtlanticMappedArena arena;
    struct GuardedResult guarded;
    const struct AtlanticBootInventory *inventory_arg;
    const struct AtlanticRetainedImage *source_arg;
    const struct AtlanticBootRequest *request_arg;
    const struct AtlanticMappedArena *arena_arg;
    struct AtlanticBootPreparation *out_arg;
};

static void defaults(struct Trial *trial, uint8_t *allocation, const uint8_t *file, size_t bytes) {
    memset(allocation,MARK,ALLOCATION_BYTES);
    memset(trial,0,sizeof(*trial));
    trial->inventory.ram_count=1;
    trial->inventory.cpu_count=1;
    trial->inventory.reserved_count=1;
    trial->inventory.ram[0]=(struct AtlanticPhysicalRange){BANK_PA,BANK_BYTES};
    trial->inventory.reserved[0]=(struct AtlanticPhysicalRange){SOURCE_PA,0x10000};
    trial->source=(struct AtlanticRetainedImage){file,SOURCE_PA,bytes};
    trial->request=(struct AtlanticBootRequest){ARENA_PA,ARENA_BYTES,504,4096,"-v",2};
    trial->arena=(struct AtlanticMappedArena){allocation+GUARD_BYTES,ARENA_PA,ARENA_BYTES};
    trial->inventory_arg=&trial->inventory;
    trial->source_arg=&trial->source;
    trial->request_arg=&trial->request;
    trial->arena_arg=&trial->arena;
    trial->out_arg=&trial->guarded.value;
    memset(&trial->guarded,MARK,sizeof(trial->guarded));
}

static void expect_failure(const struct Trial *trial, const uint8_t *allocation,
                           uint8_t *snapshot, int expected) {
    memcpy(snapshot,allocation,ALLOCATION_BYTES);
    struct GuardedResult result_snapshot;
    memcpy(&result_snapshot,&trial->guarded,sizeof(result_snapshot));
    assert(atl_xnu_image_materialize(trial->inventory_arg,trial->source_arg,trial->request_arg,
                                     trial->arena_arg,trial->out_arg)==expected);
    assert(!memcmp(allocation,snapshot,ALLOCATION_BYTES));
    assert(!memcmp(&trial->guarded,&result_snapshot,sizeof(result_snapshot)));
    ++failures;
}

int main(int argc, char **argv) {
    assert(argc==3);
    size_t image_bytes, low_bytes;
    const uint8_t *file=read_file(argv[1],&image_bytes);
    const uint8_t *low=read_file(argv[2],&low_bytes);
    assert(image_bytes==0xc000+304);
    uint8_t *allocation=malloc(ALLOCATION_BYTES);
    uint8_t *snapshot=malloc(ALLOCATION_BYTES);
    uint8_t *unusual=malloc(image_bytes+1);
    assert(allocation && snapshot && unusual);
    struct Trial trial;
    defaults(&trial,allocation,file,image_bytes);
    expect_success(allocation,&trial.inventory,&trial.source,&trial.request,&trial.arena,0);

    memcpy(unusual+1,file,image_bytes);
    size_t command=32;
    while (command+72<=image_bytes && memcmp(unusual+command+9,"__LINKEDIT",10)) {
        uint32_t size=get_le32(unusual+command+5);
        assert(size>=72 && size<=image_bytes-command);
        command+=size;
    }
    assert(command+72<=image_bytes);
    trial.source.base=unusual+1; /* Mach-O bytes may have unaligned VA backing. */
    for (size_t gap=1;gap<=5;++gap) {
        put_le64(unusual+command+25,ATL_XNU_BOOT_LINK_BASE+0x10000+gap*ATL_MACHO_PAGE_BYTES);
        expect_success(allocation,&trial.inventory,&trial.source,&trial.request,&trial.arena,gap);
    }

    defaults(&trial,allocation,file,image_bytes);
    trial.source.physical_base=ARENA_PA-image_bytes;
    expect_success(allocation,&trial.inventory,&trial.source,&trial.request,&trial.arena,0);
    trial.source.physical_base=ARENA_PA+ARENA_BYTES;
    expect_success(allocation,&trial.inventory,&trial.source,&trial.request,&trial.arena,0);

#define REJECT(change, error) do { \
    defaults(&trial,allocation,file,image_bytes); change; \
    expect_failure(&trial,allocation,snapshot,error); \
} while (0)
    REJECT(trial.inventory_arg=NULL,ATL_MATERIALIZE_ARGUMENT);
    REJECT(trial.source_arg=NULL,ATL_MATERIALIZE_ARGUMENT);
    REJECT(trial.request_arg=NULL,ATL_MATERIALIZE_ARGUMENT);
    REJECT(trial.arena_arg=NULL,ATL_MATERIALIZE_ARGUMENT);
    REJECT(trial.out_arg=NULL,ATL_MATERIALIZE_ARGUMENT);
    REJECT(trial.source.base=NULL,ATL_MATERIALIZE_ARGUMENT);
    REJECT(trial.source.bytes=0,ATL_MATERIALIZE_ARGUMENT);
    REJECT(trial.arena.base=NULL,ATL_MATERIALIZE_ARGUMENT);
    REJECT(trial.arena.bytes=0,ATL_MATERIALIZE_ARGUMENT);
    REJECT(trial.arena.bytes=SIZE_MAX,ATL_MATERIALIZE_ARGUMENT);
    REJECT(trial.source.bytes=SIZE_MAX,ATL_MATERIALIZE_ARGUMENT);
    REJECT(trial.request.command_bytes=SIZE_MAX,ATL_MATERIALIZE_ARGUMENT);
    REJECT(trial.request.command=NULL,ATL_MATERIALIZE_ARGUMENT);
    REJECT(trial.source_arg=(const void *)(allocation+1),ATL_MATERIALIZE_ARGUMENT);
    REJECT(trial.out_arg=(void *)(allocation+1),ATL_MATERIALIZE_ARGUMENT);
    REJECT(trial.arena.bytes-=1,ATL_MATERIALIZE_ARENA);
    REJECT(trial.arena.bytes+=1,ATL_MATERIALIZE_ARENA);
    REJECT(trial.arena.physical_base+=ATL_XNU_BOOT_BLOCK_BYTES,ATL_MATERIALIZE_ARENA);
    REJECT(trial.source.physical_base=0,ATL_MATERIALIZE_SOURCE);
    REJECT(trial.source.physical_base=BANK_PA+BANK_BYTES-image_bytes+1,ATL_MATERIALIZE_SOURCE);
    REJECT(trial.source.physical_base=UINT64_MAX-1,ATL_MATERIALIZE_SOURCE);
    REJECT(trial.source.physical_base=ARENA_PA-image_bytes+1,ATL_MATERIALIZE_ALIAS);
    REJECT(trial.source.physical_base=ARENA_PA+ARENA_BYTES-1,ATL_MATERIALIZE_ALIAS);
    REJECT(trial.source.physical_base=ARENA_PA+0x100,ATL_MATERIALIZE_ALIAS);
    REJECT(trial.source.base=allocation+GUARD_BYTES,ATL_MATERIALIZE_ALIAS);
    REJECT(trial.source.base=allocation+GUARD_BYTES+ARENA_BYTES-image_bytes,ATL_MATERIALIZE_ALIAS);
    REJECT(trial.request.command=(const void *)(allocation+GUARD_BYTES),ATL_MATERIALIZE_ALIAS);
    REJECT(trial.out_arg=(void *)(allocation+GUARD_BYTES),ATL_MATERIALIZE_ALIAS);
    REJECT(trial.out_arg=(void *)(allocation+GUARD_BYTES+ARENA_BYTES-sizeof(trial.guarded.value)),
           ATL_MATERIALIZE_ALIAS);
    REJECT(trial.request.command=(const void *)&trial.guarded.value,ATL_MATERIALIZE_ALIAS);
    REJECT(trial.source.base=&trial.guarded.value,ATL_MATERIALIZE_ALIAS);
    REJECT(trial.request_arg=(const void *)&trial.guarded.value,ATL_MATERIALIZE_ALIAS);
    REJECT(trial.inventory_arg=(const void *)trial.arena.base,ATL_MATERIALIZE_ALIAS);
    REJECT(trial.source_arg=(const void *)trial.arena.base,ATL_MATERIALIZE_ALIAS);
    REJECT(trial.request_arg=(const void *)trial.arena.base,ATL_MATERIALIZE_ALIAS);
    REJECT(trial.arena_arg=(const void *)trial.arena.base;
           memcpy(trial.arena.base,&trial.arena,sizeof(trial.arena)),ATL_MATERIALIZE_ALIAS);
    REJECT(trial.source.bytes=31,ATL_MACHO_BOUNDS);
    REJECT(trial.source.bytes=image_bytes-1,ATL_MACHO_LAYOUT);
    REJECT(trial.inventory.ram_count=2,ATL_PREPARE_MEMORY);
    REJECT(trial.request.retained_bytes=0,ATL_PREPARE_ARGUMENT);
    REJECT(trial.inventory.reserved[0].start=ARENA_PA,ATL_PREPARE_RESERVED);
    REJECT(trial.request.command="\n-",ATL_PREPARE_COMMAND);
    REJECT(trial.source.base=low; trial.source.bytes=low_bytes,ATL_PREPARE_PROFILE);
    REJECT(trial.source.base=unusual+1;
           memcpy(unusual+1,file,image_bytes);
           unusual[25]=5,ATL_MACHO_UNSUPPORTED);
    REJECT(trial.source.base=unusual+1;
           memcpy(unusual+1,file,image_bytes);
           put_le64(unusual+command+25,ATL_XNU_BOOT_LINK_BASE+0x1000000),ATL_PREPARE_SPACE);
#undef REJECT

    printf("PASS {\"successful\":%u,\"transactional_failures\":%u,\"hole_fixtures\":5}\n",
           successful,failures);
    free(unusual);
    free(snapshot);
    free(allocation);
    free((void *)low);
    free((void *)file);
    return 0;
}
