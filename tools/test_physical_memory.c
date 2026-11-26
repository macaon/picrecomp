// Exercise overlapping physical maps without a game or GPU: VAM allocators map their metadata page twice.
#include "../runtime/kernel.c"
#include "../runtime/nvn/objects.c"
#include <assert.h>

void rt_fatal_trace(void) { abort(); }
static uint64_t committed;
void plat_commit(uint64_t addr, uint64_t size) { (void)addr; committed += size; }

int main(void) {
    uint8_t *mem = aligned_alloc(4096, 8 * 4096);
    assert(mem);
    uint64_t base = (uint64_t)(uintptr_t)mem;
    memset(mem, 0xcc, 8 * 4096);
    region_map_physical(base + 4096, 4096);
    region_map_physical(base + 4 * 4096, 4096);
    memset(mem + 4096, 0x5a, 4096);
    memset(mem + 4 * 4096, 0xa5, 4096);
    region_map_physical(base, 7 * 4096);
    assert(committed == 7 * 4096);
    for (int i = 0; i < 8 * 4096; i++) {
        uint8_t expected = i / 4096 == 1 ? 0x5a : i / 4096 == 4 ? 0xa5 : i / 4096 == 7 ? 0xcc : 0;
        assert(mem[i] == expected);
    }
    // Remapping is a no-op, including a range wholly inside an existing mapping.
    memset(mem + 2 * 4096, 0x77, 4096);
    region_map_physical(base, 7 * 4096);
    region_map_physical(base + 2 * 4096, 4096);
    assert(mem[2 * 4096] == 0x77 && committed == 7 * 4096);
    // Unmapping and remapping a hole must provide fresh zero pages, preserving its neighbours.
    region_unmap_physical(base + 2 * 4096, 4096);
    region_map_physical(base, 7 * 4096);
    assert(committed == 8 * 4096);
    for (int i = 2 * 4096; i < 3 * 4096; i++) assert(!mem[i]);
    assert(mem[4096] == 0x5a && mem[4 * 4096] == 0xa5);
    assert(device_integer(61) == 16384 && device_integer(64) == 16384);
    free(mem);
    puts("physical memory: overlapping maps preserve data, holes zeroed, device texture limits passed");
}
