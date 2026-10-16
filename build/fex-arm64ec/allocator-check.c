/* Exercise the real FEX_IOS_HOST allocator as an x64 PE under Wine on Linux.
 * A custom memory interface simulates Wine's exact, aligned reservations in
 * the same 12 GiB band. No model of rpmalloc's allocation logic is used.
 * Include path selects either the pinned or patched rpmalloc.c. */
#include <rpmalloc.c>
#include <stdio.h>

volatile int FEX_AllocWatch_Armed;
void FEX_AllocWatch_Event(const void* p, unsigned int event) { (void)p; (void)event; }

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
#define BAND_LO ((uintptr_t)0x7d00000000ULL)
#define BAND_HI ((uintptr_t)0x8000000000ULL)
#define GRANULARITY ((size_t)0x10000)
#define HEAPS 256
#define SLOTS 191
static struct region { uintptr_t lo; size_t size; } regions[8192];
static size_t region_count, map_failures;

static uintptr_t align_up(uintptr_t v, size_t a) { return (v + a - 1) & ~(uintptr_t)(a - 1); }

static void* reserve_at(uintptr_t at, size_t size, size_t* mapped) {
    size = align_up(size, GRANULARITY);
    size_t i = 0;
    while (i < region_count && regions[i].lo < at) ++i;
    CHECK(at >= BAND_LO && at + size <= BAND_HI);
    CHECK(!i || regions[i-1].lo + regions[i-1].size <= at);
    CHECK(i == region_count || at + size <= regions[i].lo);
    CHECK(region_count < sizeof(regions) / sizeof(regions[0]));
    CHECK(VirtualAlloc((void*)at, size, MEM_RESERVE, PAGE_NOACCESS) == (void*)at);
    memmove(regions + i + 1, regions + i, (region_count - i) * sizeof(regions[0]));
    regions[i] = (struct region){at, size};
    ++region_count;
    *mapped = size;
    return (void*)at;
}

static void* band_map(size_t size, size_t alignment, size_t* offset, size_t* mapped) {
    size = align_up(size, GRANULARITY);
    if (alignment < GRANULARITY) alignment = GRANULARITY;
    uintptr_t at = BAND_LO;
    size_t i;
    for (i = 0; i <= region_count; ++i) {
        at = align_up(at, alignment);
        uintptr_t end = i < region_count ? regions[i].lo : BAND_HI;
        if (at <= end && size <= end - at) break;
        if (i < region_count) at = regions[i].lo + regions[i].size;
    }
    if (i > region_count) { ++map_failures; return 0; }
    *offset = 0;
    return reserve_at(at, size, mapped);
}

static void band_unmap(void* p, size_t offset, size_t mapped) {
    CHECK(offset == 0);
    size_t i = 0;
    while (i < region_count && regions[i].lo != (uintptr_t)p) ++i;
    CHECK(i < region_count && regions[i].size == mapped);
    CHECK(VirtualFree(p, 0, MEM_RELEASE));
    memmove(regions + i, regions + i + 1, (region_count - i - 1) * sizeof(regions[0]));
    --region_count;
}

static void band_commit(void* p, size_t size) {
    CHECK(VirtualAlloc(p, size, MEM_COMMIT, PAGE_READWRITE) == p);
}
static void band_decommit(void* p, size_t size) { CHECK(VirtualFree(p, size, MEM_DECOMMIT)); }

static void report(const char* phase, size_t completed) {
    size_t reserved = 0, gap = 0, aligned_gap = 0;
    uintptr_t at = BAND_LO;
    for (size_t i = 0; i <= region_count; ++i) {
        uintptr_t end = i < region_count ? regions[i].lo : BAND_HI;
        if (end - at > gap) gap = end - at;
        uintptr_t aligned = align_up(at, SPAN_SIZE);
        if (aligned <= end && end - aligned > aligned_gap) aligned_gap = end - aligned;
        if (i < region_count) { reserved += regions[i].size; at = end + regions[i].size; }
    }
    printf("%s span=%u heaps=%zu/%u failed_maps=%zu reserved=%.4f_MiB free=%.4f_MiB largest_gap=%.4f_MiB largest_span_aligned_gap=%.4f_MiB\n",
           phase, (unsigned)SPAN_SIZE, completed, HEAPS, map_failures,
           reserved / 1048576.0, (BAND_HI - BAND_LO - reserved) / 1048576.0,
           gap / 1048576.0, aligned_gap / 1048576.0);
}

static void verify_block(void* p, size_t size, size_t alignment) {
    CHECK(p && !((uintptr_t)p & (alignment - 1)));
    CHECK(rpmalloc_usable_size(p) >= size);
    memset(p, 0xa5, size);
    CHECK(((unsigned char*)p)[size - 1] == 0xa5);
}

static void boundaries(void) {
    /* Check every routing result through 9 MiB, including class round-up. */
    for (size_t n = 0; n <= 9 * 1024 * 1024; ++n) {
        uint32_t c = get_size_class(n);
        if (c < SIZE_CLASS_COUNT) {
            CHECK(global_size_class[c].block_count > 0);
            CHECK(global_size_class[c].block_size >= n);
        }
    }
    const size_t sizes[] = {1, 4095, 4096, 4097, 0x2a60, 256*1024-1, 256*1024,
        256*1024+1, 320*1024, 1792*1024-1, 1792*1024, 1792*1024+1,
        2*1024*1024-128, 2*1024*1024-1, 2*1024*1024, 2*1024*1024+1,
        8*1024*1024-1, 8*1024*1024, 8*1024*1024+1, 16*1024*1024+1};
    for (size_t i = 0; i < sizeof(sizes)/sizeof(sizes[0]); ++i) {
        for (size_t a = 16; a < RPMALLOC_MAX_ALIGNMENT; a *= 2) {
            void* p = rpaligned_alloc(a, sizes[i]);
            verify_block(p, sizes[i], a);
            rpfree(p);
        }
        void* p = rpcalloc(1, sizes[i]);
        CHECK(p && ((unsigned char*)p)[0] == 0 && ((unsigned char*)p)[sizes[i]-1] == 0);
        memset(p, 0x5a, sizes[i]);
        p = rprealloc(p, sizes[i] + 17);
        CHECK(p && ((unsigned char*)p)[0] == 0x5a && ((unsigned char*)p)[sizes[i]-1] == 0x5a);
        rpfree(p);
    }
    /* The page-layout change must not make realloc retain a huge mapping
     * when shrinking to a few MiB (the upstream test also checks this). */
    unsigned char* p = rpmalloc(16*1024*1024+1);
    CHECK(p);
    memset(p, 0x13, 1024);
    p = rprealloc(p, 3151000);
    CHECK(p && rpmalloc_usable_size(p) <= 4*1024*1024);
    for (size_t i = 0; i < 1024; ++i) CHECK(p[i] == 0x13);
    rpfree(p);
    /* Growing inside a huge map's usable capacity must not remap or copy.
     * Realloc also accepts an aligned pointer and realigns its data. */
    for (size_t a = 16; a < RPMALLOC_MAX_ALIGNMENT; a *= 2) {
        p = rpaligned_alloc(a, 2*1024*1024);
        CHECK(p);
        size_t capacity = rpmalloc_usable_size(p);
        memset(p, 0x19, capacity);
        size_t mappings = region_count;
        unsigned char* origin = (unsigned char*)block_get_span((block_t*)p) + SPAN_HEADER_SIZE;
        unsigned char* next = rprealloc(p, capacity);
        CHECK(next == origin && region_count == mappings);
        for (size_t i = 0; i < capacity; ++i) CHECK(next[i] == 0x19);
        rpfree(next);
    }
    printf("PASS routing=9437185 sizes=20 alignments=14 calloc/realloc/free huge_shrink huge_reuse\n");
}

static void stress(void) {
    rpmalloc_heap_t* heaps[HEAPS];
    void* blocks[HEAPS][3] = {{0}};
    void* furniture[SLOTS][2];
    size_t furniture_size[SLOTS][2];
    const size_t sizes[3] = {473, 0x2a60, 320*1024};
    size_t completed = 0;
    /* Allocate heap metadata before pressure so a failed span is the failure. */
    for (size_t i = 0; i < HEAPS; ++i) { heaps[i] = rpmalloc_heap_acquire(); CHECK(heaps[i]); }
    /* Scatter 8 MiB + guard and 2 MiB reservations at 64 MiB intervals.
     * Their sizes come from run6, their placement is synthetic. */
    for (size_t i = 0; i < SLOTS; ++i) {
        uintptr_t at = BAND_LO + (i+1) * 64*1024*1024;
        furniture[i][0] = reserve_at(at, 8*1024*1024+8192, &furniture_size[i][0]);
        furniture[i][1] = reserve_at(at + furniture_size[i][0], 2*1024*1024, &furniture_size[i][1]);
    }
    int first_failure = 1;
    for (size_t i = 0; i < HEAPS; ++i) {
        int all = 1;
        for (size_t j = 0; j < 3; ++j) {
            void* p = blocks[i][j] = rpmalloc_heap_alloc(heaps[i], sizes[j]);
            if (!p) { all = 0; continue; }
            CHECK(rpmalloc_usable_size(p) >= sizes[j]);
            ((unsigned char*)p)[0] = (unsigned char)i;
            ((unsigned char*)p)[sizes[j]-1] = (unsigned char)(i+1);
        }
        completed += all;
        if (!all && first_failure) { report("first_failure", completed); first_failure = 0; }
    }
    report("pressure", completed);
    CHECK((SPAN_SIZE == 2*1024*1024) ? completed == HEAPS : completed < HEAPS);
    /* Free half the ordinary blocks, reuse their pages, preserve other heaps. */
    for (size_t i = 0; i < HEAPS; ++i) {
        for (size_t j = 0; j < 3; ++j) {
            unsigned char* p = blocks[i][j];
            if (!p) continue;
            CHECK(p[0] == (unsigned char)i && p[sizes[j]-1] == (unsigned char)(i+1));
            if (!(i & 1)) {
                rpmalloc_heap_free(heaps[i], p);
                p = rpmalloc_heap_alloc(heaps[i], sizes[j]);
                CHECK(p);
            }
        }
    }
    for (size_t i = 0; i < HEAPS; ++i) {
        rpmalloc_heap_free_all(heaps[i]);
        rpmalloc_heap_release(heaps[i]);
    }
    for (size_t i = 0; i < SLOTS; ++i) {
        for (size_t j = 0; j < 2; ++j) band_unmap(furniture[i][j], 0, furniture_size[i][j]);
    }
    report("released_spans", completed);
}

int main(int argc, char** argv) {
    ios_fex_band_base = BAND_LO;
    ios_fex_band_end = BAND_HI-1;
    rpmalloc_interface_t iface = {0};
    iface.memory_map = band_map;
    iface.memory_unmap = band_unmap;
    iface.memory_commit = band_commit;
    iface.memory_decommit = band_decommit;
    rpmalloc_config_t config = {0};
    config.unmap_on_finalize = 1;
    CHECK(rpmalloc_initialize_config(&iface, &config) == 0);
    if (argc > 1 && strcmp(argv[1], "boundaries") == 0) boundaries();
    else stress();
    rpmalloc_finalize();
    CHECK(region_count == 0);
    puts("PASS all mappings released");
    return 0;
}
