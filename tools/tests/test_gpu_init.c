/* Exercise the production initialization and transport-selection functions. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t registers[16];
static int has_cpu_ring, reject_selection;
static volatile uint32_t *test_register(unsigned offset);
#include "of_gpu_test.h"
#include "doom_init.h"

static volatile uint32_t *test_register(unsigned offset)
{
    /* Apply the previous control write before the next bus access. */
    uint32_t control = registers[0];
    registers[0] = 0;
    if (control & 6u)
        registers[0x14 / 4] = GPU_STATUS_RING_EMPTY |
            (has_cpu_ring ? GPU_STATUS_CPU_RING_CAP : 0);
    if ((control & GPU_CTRL_CPU_RING_SELECT) && has_cpu_ring && !reject_selection)
        registers[0x14 / 4] |= GPU_STATUS_CPU_RING_MODE;
    return &registers[offset / 4];
}

static void initialize(int capability, int refused, uint32_t completed)
{
    struct of_capabilities caps = {0};
    caps.gpu_base = 0x80000000u;
    caps.sdram_base = 1;
    caps.sdram_size = UINT32_MAX - 1u;
    has_cpu_ring = capability;
    reject_selection = refused;
    /* Simulate a previous game leaving both cached headers and commands. */
    _gpu_cpu_ring = 1;
    _gpu_span_hdr_valid = 1;
    _gpu_state_valid = UINT32_MAX;
    _gpu_cmd_words = 100;
    _gpu_batch_inflight_mask = 3;
    _gpu_unflushed_sync = 1;
    _gpu_wrptr = 1024;
    registers[0x18 / 4] = completed;
    assert(gpu_init_for_doom(&caps));
    assert(_gpu_cpu_ring == (capability && !refused));
    assert(!_gpu_span_hdr_valid && !_gpu_state_valid);
    assert(!_gpu_cmd_words && !_gpu_batch_inflight_mask && !_gpu_unflushed_sync);
    assert(!_gpu_wrptr && !_gpu_known_rdptr && _gpu_fence_next == completed + 1u);
    assert(!of_gpu_fence_reached(_gpu_fence_next));
    assert(_gpu_batch_buf == &_gpu_batch_storage[0][0]);
    assert(_gpu_batch_dma_addr == (uintptr_t)_gpu_batch_buf);
    assert(GPU_PALOOKUP_BASE == (uintptr_t)_gpu_palookup_storage);
    assert(GPU_TEX_FLUSH == 1);
}

int main(void)
{
    initialize(0, 0, 0); /* Pocket and older MiSTer cores. */
    initialize(1, 0, 1);
    initialize(1, 1, 77); /* Capability alone must not select the transport. */
    initialize(0, 0, 0x7fffffff); /* Discard previous mode, preserve fence ordering. */
    initialize(1, 0, UINT32_MAX);
    _gpu_cpu_ring = 0;
    _gpu_cmd_words = 1;
    assert(!of_gpu_use_cpu_ring());
    _gpu_cmd_words = 0;
    _gpu_batch_inflight_mask = 1;
    assert(!of_gpu_use_cpu_ring());
    _gpu_batch_inflight_mask = 0;
    registers[0x14 / 4] |= GPU_STATUS_DMA_BUSY;
    assert(!of_gpu_use_cpu_ring());
    registers[0x14 / 4] &= ~GPU_STATUS_DMA_BUSY;
    assert(of_gpu_use_cpu_ring());
    puts("PASS GPU initialization: DMA fallback, direct transport, rejected selection, reinitialization and busy submission");
    return 0;
}
