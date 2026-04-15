#include "test.h"
#include "of_mixer.h"
#include <time.h>

/* PSRAM / SRAM memory map (cached CPU-side addresses)
 * CRAM0 cached:    0x30000000 – 0x30FFFFFF (16 MB)
 * CRAM1 uncached:  0x39000000 – 0x39FFFFFF (16 MB, bridge-accessible)
 * SRAM:            0x3A000000 – 0x3A03FFFF (256 KB)
 *
 * Uncached mirror: base | 0x08000000  (bypasses D-cache)
 * e.g. CRAM0 uncached = 0x38000000
 *
 * CRAM1 is FULLY partitioned by the OS:
 *   0x000000 – 0x27FFFF  saves (10 slots × 256 KB) — bridge-persisted
 *   0x280000 – 0x28FFFF  FTAB (filename table) — bridge-written
 *   0x290000 – 0x2FFFFF  file-IO scratch (~448 KB)
 *   0x300000 – 0x3FFFFF  I/O cache
 *   0x400000 – 0xEFFFFF  audio samples (11 MB)
 *   0xF00000 – 0xFFFFFF  AUDIO_SCRATCH_CRAM1 (1 MB) — mixer scratch
 *
 * Picking ANY region the bridge / mixer / file layer touches is
 * non-idempotent across iterations: the original test wrote slot 0
 * (saves) which the bridge then re-read on the next pass; the unused
 * tail turned out to be the audio scratch buffer.
 *
 * file-IO scratch is the only region that is (a) not bridge-managed
 * and (b) only touched during user-initiated file ops. test_psram_memory
 * runs BEFORE any file test, so its readback can't race the OS — and
 * between iterations the test's own pattern is the last write to land. */
#define CRAM0_CACHED_BASE   0x30000000  /* CRAM0 cached base */
#define CRAM0_UNCACHED_BASE 0x38000000  /* CRAM0 uncached base */
#define CRAM0_TEST_OFFSET   0x00800000  /* 8 MB offset — avoids slot table at base */
#define CRAM1_UNCACHED_BASE 0x39000000  /* CRAM1 uncached (bridge-accessible) */
#define CRAM1_TEST_OFFSET   0x00290000  /* file-IO scratch — see comment above */
#define SRAM_BASE           0x3A000000  /* On-chip SRAM */

/* SDRAM region used to evict D-cache lines (must not overlap app code/data) */
#define SDRAM_EVICT_BASE    0x13F00000

void test_psram_memory(void) {
    section_start("PSRAM Mem");

    /* This whole test bangs on hardcoded CRAM/SRAM addresses that
     * only exist on the Pocket target. On any other platform we
     * skip cleanly so the suite still completes. */
    if (of_get_caps()->platform_id != OF_PLATFORM_POCKET) {
        test_pass("not pocket");
        section_end();
        return;
    }

    /* DIAGNOSTIC: stop any lingering mixer voices to isolate whether the
     * CRAM1 hang on iteration 2+ is caused by mixer state bleed-through.
     * If this makes the hang go away, mixer is the culprit. */
    of_mixer_stop_all();

    volatile uint32_t *cram0 = (volatile uint32_t *)(CRAM0_CACHED_BASE + CRAM0_TEST_OFFSET);
    volatile uint32_t *cram1 = (volatile uint32_t *)(CRAM1_UNCACHED_BASE + CRAM1_TEST_OFFSET);
    // SRAM removed — GPU-exclusive (Z-buffer)

    cram0[0] = 0xDEADBEEF;
    cram0[1] = 0xCAFEBABE;
    ASSERT("cram0 w/r[0]", cram0[0] == 0xDEADBEEF);
    ASSERT("cram0 w/r[1]", cram0[1] == 0xCAFEBABE);

    {
        int ok = 1;
        for (int i = 0; i < 32; i++)
            cram0[i] = 1u << i;
        for (int i = 0; i < 32; i++) {
            if (cram0[i] != (1u << i)) { ok = 0; break; }
        }
        ASSERT("cram0 walk1", ok);
    }

    {
        int ok = 1;
        for (uint32_t i = 0; i < 16384; i++)
            cram0[i] = i ^ 0x5A5A5A5A;
        for (uint32_t i = 0; i < 16384; i++) {
            if (cram0[i] != (i ^ 0x5A5A5A5A)) { ok = 0; break; }
        }
        ASSERT("cram0 64K", ok);
    }

    {
        for (uint32_t i = 0; i < 16384; i++)
            cram0[i] = i;

        uint32_t t0 = of_time_us();
        volatile uint32_t sum = 0;
        for (uint32_t i = 0; i < 16384; i++)
            sum += cram0[i];
        uint32_t t1 = of_time_us();
        (void)sum;

        uint32_t elapsed_us = t1 - t0;
        ASSERT("cram0 speed", elapsed_us < 100000);
        (void)elapsed_us;
    }

    cram1[0] = 0x12345678;
    cram1[1] = 0x9ABCDEF0;
    ASSERT("cram1 w/r[0]", cram1[0] == 0x12345678);
    ASSERT("cram1 w/r[1]", cram1[1] == 0x9ABCDEF0);

    {
        /* Snapshot the test region so the test is fully idempotent.
         * No CRAM1 region is truly idle across iterations: saves are
         * bridge-persisted, file-IO scratch and I/O cache are touched
         * by file ops, samples are touched by the mixer, the audio
         * scratch tail is touched by the streamer. Whatever was here
         * before us is restored on the way out, and the OS sees no
         * disturbance from one iteration to the next.
         *
         * 16 KB lives in BSS (SDRAM), not on the BRAM stack. */
        static uint32_t cram1_save[4096];
        for (uint32_t i = 0; i < 4096; i++)
            cram1_save[i] = cram1[i];

        int ok = 1;
        uint32_t fail_at = 0, fail_exp = 0, fail_got = 0;
        for (uint32_t i = 0; i < 4096; i++)
            cram1[i] = ~i;
        for (uint32_t i = 0; i < 4096; i++) {
            uint32_t got = cram1[i];
            uint32_t exp = ~i;
            if (got != exp) {
                ok = 0;
                fail_at = i;
                fail_exp = exp;
                fail_got = got;
                break;
            }
        }

        /* Restore original contents BEFORE asserting so the failure
         * detail printf doesn't allocate / call into anything that
         * might re-touch CRAM1 with the test pattern still in place. */
        for (uint32_t i = 0; i < 4096; i++)
            cram1[i] = cram1_save[i];

        if (ok) {
            test_pass("cram1 16K");
        } else {
            snprintf(__buf, sizeof(__buf),
                     "@%lu e%08lx g%08lx",
                     (unsigned long)fail_at,
                     (unsigned long)fail_exp,
                     (unsigned long)fail_got);
            test_fail("cram1 16K", __buf);
        }
    }

    // SRAM tests removed — SRAM is GPU-exclusive (Z-buffer)

    {
        cram1[0] = 0x11111111;
        cram0[0] = 0x22222222;
        ASSERT("isolation c1", cram1[0] == 0x11111111);
        ASSERT("isolation c0", cram0[0] == 0x22222222);
    }

    {
        volatile uint8_t *cram0_b = (volatile uint8_t *)(CRAM0_CACHED_BASE + CRAM0_TEST_OFFSET);
        cram0[0] = 0x00000000;
        cram0_b[1] = 0xAB;
        ASSERT("byte write", (cram0[0] & 0x0000FF00) == 0x0000AB00);
    }

    {
        int ok = 1;
        for (int i = 0; i < 256; i++) {
            cram0[i] = (uint32_t)i;
            cram1[i] = (uint32_t)(i + 0x1000);
        }
        for (int i = 0; i < 256; i++) {
            if (cram0[i] != (uint32_t)i) { ok = 0; break; }
            if (cram1[i] != (uint32_t)(i + 0x1000)) { ok = 0; break; }
        }
        ASSERT("interleave", ok);
    }

    section_end();
}

void test_cram0_256k(void) {
    section_start("CRAM0 256K");

    if (of_get_caps()->platform_id != OF_PLATFORM_POCKET) {
        test_pass("not pocket");
        section_end();
        return;
    }

    /* Uncached 32-bit write then read */
    {
        volatile uint32_t *u = (volatile uint32_t *)(CRAM0_UNCACHED_BASE + CRAM0_TEST_OFFSET);
        u[0] = 0xDEAD1234;
        __asm__ volatile("fence" ::: "memory");
        uint32_t v = u[0];
        ASSERT("uc w/r", v == 0xDEAD1234);
    }

    /* Write via cached alias, read via uncached alias to bypass D-cache.
     * This tests whether PSRAM writeback actually reaches the chip. */
    volatile uint32_t *cram0_c = (volatile uint32_t *)(CRAM0_CACHED_BASE + CRAM0_TEST_OFFSET);
    volatile uint32_t *cram0_u = (volatile uint32_t *)(CRAM0_UNCACHED_BASE + CRAM0_TEST_OFFSET);
    const uint32_t count = 256 * 1024 / 4;  /* 65536 words = 256KB */

    /* Write pass: XOR pattern via cached alias */
    for (uint32_t i = 0; i < count; i++)
        cram0_c[i] = i ^ 0xA5A5A5A5;

    /* Flush D-cache: read 128KB from SDRAM to evict all dirty lines */
    volatile char *evict = (volatile char *)SDRAM_EVICT_BASE;
    for (uint32_t i = 0; i < 131072; i += 64)
        (void)evict[i];
    __asm__ volatile("fence" ::: "memory");

    /* Read-back via UNCACHED alias — goes directly to PSRAM */
    {
        int ok = 1;
        uint32_t fail_addr = 0, fail_exp = 0, fail_got = 0;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t exp = i ^ 0xA5A5A5A5;
            uint32_t got = cram0_u[i];
            if (got != exp) {
                ok = 0;
                fail_addr = i;
                fail_exp = exp;
                fail_got = got;
                break;
            }
        }
        if (ok) {
            test_pass("xor pattern");
        } else {
            snprintf(__buf, sizeof(__buf),
                     "@%lu exp %08lx go %08lx",
                     (unsigned long)fail_addr,
                     (unsigned long)fail_exp,
                     (unsigned long)fail_got);
            test_fail("xor pattern", __buf);
        }
    }

    /* Write pass 2: inverted index via cached */
    for (uint32_t i = 0; i < count; i++)
        cram0_c[i] = ~i;

    /* Flush D-cache again */
    for (uint32_t i = 0; i < 131072; i += 64)
        (void)evict[i];
    __asm__ volatile("fence" ::: "memory");

    /* Read-back pass 2 via uncached */
    {
        int ok = 1;
        uint32_t fail_addr = 0, fail_exp = 0, fail_got = 0;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t exp = ~i;
            uint32_t got = cram0_u[i];
            if (got != exp) {
                ok = 0;
                fail_addr = i;
                fail_exp = exp;
                fail_got = got;
                break;
            }
        }
        if (ok) {
            test_pass("inv pattern");
        } else {
            snprintf(__buf, sizeof(__buf),
                     "@%lu exp %08lx go %08lx",
                     (unsigned long)fail_addr,
                     (unsigned long)fail_exp,
                     (unsigned long)fail_got);
            test_fail("inv pattern", __buf);
        }
    }

    section_end();
}
