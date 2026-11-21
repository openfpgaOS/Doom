/* [r_cpufb] Word-level CPU framebuffer line tracking for Doom's r_gpu.c.
 *
 * Drop-in replacements for gpu_mark_cpu_dirty_rect, gpu_invalidate_rect_for_cpu
 * and gpu_flush_cpu_dirty_lines that operate on 32 lines per bitmap word
 * instead of one gpu_line_test/gpu_line_set call per 64-byte line.
 * Contract (checked by test_lines.c against the original code):
 *   - final gpu_cpu_dirty_lines / gpu_cpu_valid_lines bitmaps are identical;
 *   - the set of lines passed to gpu_inval_line_run / gpu_flush_line_run is
 *     identical (flush runs: identical maximal runs; invalidate runs: same
 *     lines, grouped per row exactly as before except that the rows of a
 *     full-width rect form one contiguous range).
 * Rows covering at most GPU_LINES_NARROW lines (HUD glyphs, status bar
 * widgets) keep the original per-line loops; wider rows and full-width rects
 * use whole-word operations.
 * Requires before inclusion: gpu_line_set(), gpu_line_test(), SCREENWIDTH, SCREENHEIGHT, GPU_FB_CACHE_LINE_BYTES,
 * GPU_FB_CACHE_LINES, GPU_FB_CACHE_WORDS, gpu_cpu_dirty_lines[],
 * gpu_cpu_valid_lines[], gpu_clip_rect(), gpu_inval_line_run(),
 * gpu_flush_line_run().
 */
static inline __attribute__((always_inline)) uint32_t gpu_lines_mask(unsigned int word, unsigned int first, unsigned int last)
{
    /* bits of word `word` covering absolute lines [first, last] */
    unsigned int lo = word << 5;
    unsigned int a = first > lo ? first - lo : 0u;
    unsigned int b = last < lo + 31u ? last - lo : 31u;
    uint32_t m = b == 31u ? 0xFFFFFFFFu : ((1u << (b + 1u)) - 1u);
    return m & ~((1u << a) - 1u);
}

static inline __attribute__((always_inline)) void gpu_lines_set_range(uint32_t *bits, unsigned int first, unsigned int last)
{
    for (unsigned int w = first >> 5; w <= (last >> 5); w++)
        bits[w] |= gpu_lines_mask(w, first, last);
}

/* Invalidate (and mark valid) every line in [first, last] that is neither
 * CPU-dirty nor already CPU-valid, one gpu_inval_line_run per maximal run. */
static void gpu_lines_inval_range_slow(unsigned int first, unsigned int last);
#define GPU_LINES_NARROW 4u   /* <= this many lines per row: original per-line loop */
static inline __attribute__((always_inline)) void gpu_lines_inval_range(unsigned int first, unsigned int last)
{
    if (last - first < GPU_LINES_NARROW)
    {
        /* original per-line code (Doom 1.2.2), cheapest for short rows */
        unsigned int line = first;
        while (line <= last)
        {
            unsigned int run_first;
            while (line <= last &&
                   (gpu_line_test(gpu_cpu_dirty_lines, line) ||
                    gpu_line_test(gpu_cpu_valid_lines, line)))
                line++;
            if (line > last)
                break;
            run_first = line;
            while (line <= last &&
                   !gpu_line_test(gpu_cpu_dirty_lines, line) &&
                   !gpu_line_test(gpu_cpu_valid_lines, line))
            {
                gpu_line_set(gpu_cpu_valid_lines, line);
                line++;
            }
            gpu_inval_line_run(run_first, line - 1u);
        }
        return;
    }
    gpu_lines_inval_range_slow(first, last);
}

static void gpu_lines_inval_range_slow(unsigned int first, unsigned int last)
{
    unsigned int line = first;
    while (line <= last)
    {
        unsigned int w = line >> 5;
        uint32_t m = gpu_lines_mask(w, line, last);
        uint32_t need = m & ~(gpu_cpu_dirty_lines[w] | gpu_cpu_valid_lines[w]);
        unsigned int run_first;

        if (!need)
        {
            line = (w + 1u) << 5;
            continue;
        }
        run_first = (w << 5) + (unsigned int)R_Ctz32(need);
        line = run_first;
        for (;;)
        {
            unsigned int ww = line >> 5;
            uint32_t mm = gpu_lines_mask(ww, line, last);
            uint32_t nd = mm & ~(gpu_cpu_dirty_lines[ww] | gpu_cpu_valid_lines[ww]);
            /* consecutive needing lines starting at `line` */
            uint32_t from = nd >> (line & 31u);
            unsigned int n = ~from ? (unsigned int)R_Ctz32(~from) : 32u - (line & 31u);
            if (n > 32u - (line & 31u))
                n = 32u - (line & 31u);
            if (n == 0u)
                break;
            gpu_cpu_valid_lines[ww] |= gpu_lines_mask(ww, line, line + n - 1u);
            line += n;
            if (line > last || (line & 31u) != 0u)
                break;
        }
        gpu_inval_line_run(run_first, line - 1u);
    }
}

/* A rect whose every row lies inside one 64-byte line (a CPU column): its
 * lines step by GPU_LINES_PER_ROW, so a bitmap word takes them all at once
 * through a residue mask.  Same lines, same order as the per-row loops. */
#define GPU_LINES_PER_ROW (SCREENWIDTH / GPU_FB_CACHE_LINE_BYTES)
static uint32_t gpu_lines_col_pat[GPU_LINES_PER_ROW];   /* bit b set iff b % GPU_LINES_PER_ROW == k */

static inline __attribute__((always_inline)) int gpu_rect_is_column(int x, int w)
{
    return (SCREENWIDTH % GPU_FB_CACHE_LINE_BYTES) == 0
        && ((unsigned int)x % GPU_FB_CACHE_LINE_BYTES) + (unsigned int)w <= GPU_FB_CACHE_LINE_BYTES;
}

/* Lines first, first + GPU_LINES_PER_ROW, ... <= last that fall in word w. */
static inline __attribute__((always_inline)) uint32_t gpu_lines_col_mask(unsigned int w, unsigned int first, unsigned int last)
{
    unsigned int ph = (first % GPU_LINES_PER_ROW + GPU_LINES_PER_ROW
                       - (w << 5) % GPU_LINES_PER_ROW) % GPU_LINES_PER_ROW;

    if (!gpu_lines_col_pat[0])
        for (unsigned int b = 0; b < 32u; b++)
            gpu_lines_col_pat[b % GPU_LINES_PER_ROW] |= 1u << b;
    return gpu_lines_col_pat[ph] & gpu_lines_mask(w, first, last);
}

static void gpu_invalidate_rect_for_cpu(int x, int y, int w, int h)
{
    unsigned int cache_start;

    if (gpu_draw_render_base == NULL)
        return;
    if (!gpu_clip_rect(&x, &y, &w, &h))
        return;

    cache_start = R_Perf_BeginStage();

    if (x == 0 && w == SCREENWIDTH && (SCREENWIDTH % GPU_FB_CACHE_LINE_BYTES) == 0)
    {
        /* whole rows: one contiguous line range */
        gpu_lines_inval_range((unsigned int)y * (SCREENWIDTH / GPU_FB_CACHE_LINE_BYTES),
                              (unsigned int)(y + h) * (SCREENWIDTH / GPU_FB_CACHE_LINE_BYTES) - 1u);
    }
    else if (h > 1 && gpu_rect_is_column(x, w))
    {
        unsigned int first = ((unsigned int)y * SCREENWIDTH + (unsigned int)x) / GPU_FB_CACHE_LINE_BYTES;
        unsigned int last = first + (unsigned int)(h - 1) * GPU_LINES_PER_ROW;

        for (unsigned int wd = first >> 5; wd <= (last >> 5); wd++)
        {
            uint32_t need = gpu_lines_col_mask(wd, first, last)
                          & ~(gpu_cpu_dirty_lines[wd] | gpu_cpu_valid_lines[wd]);

            gpu_cpu_valid_lines[wd] |= need;
            while (need)
            {
                unsigned int line = (wd << 5) + (unsigned int)__builtin_ctz(need);
                gpu_inval_line_run(line, line);
                need &= need - 1u;
            }
        }
    }
    else
    {
        unsigned int row_off = (unsigned int)y * SCREENWIDTH;
        unsigned int first = (row_off + (unsigned int)x) / GPU_FB_CACHE_LINE_BYTES;
        unsigned int last = (row_off + (unsigned int)x + (unsigned int)w - 1u) / GPU_FB_CACHE_LINE_BYTES;
        for (int row = 0; row < h; row++)
        {
            gpu_lines_inval_range(first, last);
            if ((SCREENWIDTH % GPU_FB_CACHE_LINE_BYTES) == 0)
            {
                first += SCREENWIDTH / GPU_FB_CACHE_LINE_BYTES;
                last += SCREENWIDTH / GPU_FB_CACHE_LINE_BYTES;
            }
            else
            {
                row_off += SCREENWIDTH;
                first = (row_off + (unsigned int)x) / GPU_FB_CACHE_LINE_BYTES;
                last = (row_off + (unsigned int)x + (unsigned int)w - 1u) / GPU_FB_CACHE_LINE_BYTES;
            }
        }
    }

    R_Perf_EndStage(R_PERF_STAGE_CACHE, cache_start);
}

static void gpu_mark_cpu_dirty_rect(int x, int y, int w, int h)
{
    if (!gpu_clip_rect(&x, &y, &w, &h))
        return;

    if (x == 0 && w == SCREENWIDTH && (SCREENWIDTH % GPU_FB_CACHE_LINE_BYTES) == 0)
    {
        unsigned int first = (unsigned int)y * (SCREENWIDTH / GPU_FB_CACHE_LINE_BYTES);
        unsigned int last = (unsigned int)(y + h) * (SCREENWIDTH / GPU_FB_CACHE_LINE_BYTES) - 1u;
        gpu_lines_set_range(gpu_cpu_dirty_lines, first, last);
        gpu_lines_set_range(gpu_cpu_valid_lines, first, last);
    }
    else if (h > 1 && gpu_rect_is_column(x, w))
    {
        unsigned int first = ((unsigned int)y * SCREENWIDTH + (unsigned int)x) / GPU_FB_CACHE_LINE_BYTES;
        unsigned int last = first + (unsigned int)(h - 1) * GPU_LINES_PER_ROW;

        for (unsigned int wd = first >> 5; wd <= (last >> 5); wd++)
        {
            uint32_t m = gpu_lines_col_mask(wd, first, last);

            gpu_cpu_dirty_lines[wd] |= m;
            gpu_cpu_valid_lines[wd] |= m;
        }
    }
    else
    {
        unsigned int row_off = (unsigned int)y * SCREENWIDTH;
        unsigned int first = (row_off + (unsigned int)x) / GPU_FB_CACHE_LINE_BYTES;
        unsigned int last = (row_off + (unsigned int)x + (unsigned int)w - 1u) / GPU_FB_CACHE_LINE_BYTES;
        for (int row = 0; row < h; row++)
        {
            if (last - first < GPU_LINES_NARROW)
            {
                for (unsigned int line = first; line <= last; line++)
                {
                    gpu_line_set(gpu_cpu_dirty_lines, line);
                    gpu_line_set(gpu_cpu_valid_lines, line);
                }
            }
            else
            {
                gpu_lines_set_range(gpu_cpu_dirty_lines, first, last);
                gpu_lines_set_range(gpu_cpu_valid_lines, first, last);
            }
            if ((SCREENWIDTH % GPU_FB_CACHE_LINE_BYTES) == 0)
            {
                first += SCREENWIDTH / GPU_FB_CACHE_LINE_BYTES;
                last += SCREENWIDTH / GPU_FB_CACHE_LINE_BYTES;
            }
            else
            {
                row_off += SCREENWIDTH;
                first = (row_off + (unsigned int)x) / GPU_FB_CACHE_LINE_BYTES;
                last = (row_off + (unsigned int)x + (unsigned int)w - 1u) / GPU_FB_CACHE_LINE_BYTES;
            }
        }
    }

    gpu_cpu_dirty = 1;
    gpu_write_prepared = 0;
}

/* Flush every maximal run of CPU-dirty lines (same runs as the bit loop). */
static void gpu_flush_cpu_dirty_lines(void)
{
    unsigned int cache_start;
    unsigned int run_first = 0;
    int in_run = 0;

    if (!gpu_cpu_dirty || gpu_draw_render_base == NULL)
        return;

    cache_start = R_Perf_BeginStage();

    for (unsigned int w = 0; w < GPU_FB_CACHE_WORDS; w++)
    {
        uint32_t bits = gpu_cpu_dirty_lines[w];
        unsigned int pos = 0;
        if (w == GPU_FB_CACHE_WORDS - 1u && (GPU_FB_CACHE_LINES & 31u))
            bits &= (1u << (GPU_FB_CACHE_LINES & 31u)) - 1u;
        if (!in_run && !bits)
            continue;
        if (in_run && bits == 0xFFFFFFFFu)
            continue;
        while (pos < 32u)
        {
            uint32_t rest = bits >> pos;
            if (!in_run)
            {
                if (!rest)
                    break;
                pos += (unsigned int)R_Ctz32(rest);
                run_first = (w << 5) + pos;
                in_run = 1;
            }
            else
            {
                uint32_t inv = ~rest;
                unsigned int n = inv ? (unsigned int)R_Ctz32(inv) : 32u;
                if (pos + n >= 32u)
                    break;      /* run continues into the next word */
                pos += n;
                gpu_flush_line_run(run_first, (w << 5) + pos - 1u);
                in_run = 0;
            }
        }
    }
    if (in_run)
        gpu_flush_line_run(run_first, GPU_FB_CACHE_LINES - 1u);

    R_Perf_EndStage(R_PERF_STAGE_CACHE, cache_start);
    gpu_clear_line_bits(gpu_cpu_dirty_lines);
    gpu_cpu_dirty = 0;
}
