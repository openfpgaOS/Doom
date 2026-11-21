/* Draw random masked midtexture columns twice: through the production CPU post
 * walk (R_DrawMaskedColumn -> R_DrawColumn / R_DrawClampedMaskedColumn) and
 * through R_DrawMaskedColumnGPU, whose affine columns are emulated as the GPU
 * column command samples them: texel ((t + k * tstep) >> 16) & 127 of the
 * column source, lit by the palookup row.  The two framebuffers must match. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int boolean;
typedef int32_t fixed_t;
typedef uint8_t byte;
typedef uint8_t pixel_t;
typedef pixel_t lighttable_t;
typedef struct { byte topdelta, length; } column_t;
#define true 1
#define false 0
#define FRACBITS 16
#define FRACUNIT 65536
#define SCREENWIDTH 320
#define SCREENHEIGHT 200
static int viewheight, centery, detailshift, dc_x, dc_yl, dc_yh, maskedcolormaprow;
static fixed_t dc_texturemid, dc_iscale, spryscale, sprtopscreen;
static byte *dc_source, *dc_translation;
static lighttable_t *dc_colormap;
static short mfloorclip[SCREENWIDTH], mceilingclip[SCREENWIDTH];
static pixel_t screen[2][SCREENWIDTH * SCREENHEIGHT], *ylookup[SCREENHEIGHT];
static int columnofs[SCREENWIDTH];
static lighttable_t maps[64][256];
static unsigned cpu_columns, gpu_columns, gpu_segments, clamped_posts, declined;  /* posts, segments, columns */
static void R_DrawColumnLow(void) { abort(); }
static void R_DrawTranslatedColumn(void) { abort(); }
static void R_DrawTranslatedColumnLow(void) { abort(); }
static void R_DrawColumn(void);
static void (*colfunc)(void) = R_DrawColumn;
static void (*basecolfunc)(void) = R_DrawColumn;
static boolean R_GPU_DrawColumn(void) { return false; }
static boolean R_GPU_MaskedPost(int x, int yl, int yh) { (void)x; (void)yl; (void)yh; return false; }
static boolean R_GPU_SpritePost(int x, int yl, int yh) { (void)x; (void)yl; (void)yh; return false; }
static boolean R_GPU_DrawColumnLightDirect(int x, int yl, int yh, const byte *s,
                                           int mid, int step, int light)
{ (void)x; (void)yl; (void)yh; (void)s; (void)mid; (void)step; (void)light; return false; }
static void R_PrepareViewRectForCPU(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
static void R_Perf_CountCpuColumn(unsigned n) { (void)n; cpu_columns++; }
static int gpu_ok;
static boolean R_GPU_CanDrawColumn(int x, int yl, int yh, int light)
{
    return gpu_ok && x >= 0 && x < SCREENWIDTH && yl >= 0 && yh < SCREENHEIGHT
        && yh - yl + 1 <= 4095 && light >= 0 && light < 64;
}
static void R_GPU_DrawColumnSegments(int x, int yl, int nseg, const int *count,
                                     const byte *source, const int32_t *t,
                                     const int32_t *tstep, int light)
{
    for (int i = 0; i < nseg; i++) {
        uint32_t tt = (uint32_t)t[i];
        assert(count[i] > 0);
        if (nseg > 1 && i == 0) clamped_posts++;
        for (int k = 0; k < count[i]; k++, yl++, tt += (uint32_t)tstep[i])
            screen[1][yl * SCREENWIDTH + x] = maps[light][source[(tt >> 16) & 127]];
        gpu_segments++;
    }
    gpu_columns++;
}
#include "masked_functions.h"

static unsigned seed = 0x5eed1e55;
static unsigned rnd(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }

/* Scale, texture offset and clips with the production relations
 * (iscale = 0xffffffff / scale, sprtopscreen = centery - texturemid*scale). */
static void draw_params(int trial, int height)
{
    spryscale = trial % 17 ? 256 + rnd() % (12 * FRACUNIT) : 1 + rnd() % 4096;
    if (trial % 3 == 1) spryscale = (fixed_t)((FRACUNIT / 16) << (rnd() % 10)) + (fixed_t)(rnd() % FRACUNIT);
    if (trial % 101 == 0) spryscale = 1;          /* iscale wraps negative */
    dc_iscale = 0xffffffffu / (unsigned)spryscale;
    /* Map-unit (integer) texture offsets are what walls usually see. */
    dc_texturemid = (int32_t)(rnd() % (height * FRACUNIT * 3)) - height * FRACUNIT;
    if (trial & 1) dc_texturemid &= ~(FRACUNIT - 1);
    sprtopscreen = centery * FRACUNIT - (fixed_t)(((int64_t)dc_texturemid * spryscale) >> FRACBITS);
    mceilingclip[dc_x] = (short)((int)(rnd() % (viewheight / 2)) - 1);
    mfloorclip[dc_x] = (short)(viewheight / 2 + rnd() % (viewheight / 2 + 1));
}

/* True if a visible post of col samples outside itself (R_DrawClampedMaskedColumn's test). */
static int clamps(const column_t *post)
{
    for (; post->topdelta != 0xff; post = (const column_t *)((const byte *)post + post->length + 4)) {
        int top = sprtopscreen + spryscale * post->topdelta, bottom = top + spryscale * post->length;
        int yl = (top + FRACUNIT - 1) >> FRACBITS, yh = (bottom - 1) >> FRACBITS;
        if (yh >= mfloorclip[dc_x]) yh = mfloorclip[dc_x] - 1;
        if (yl <= mceilingclip[dc_x]) yl = mceilingclip[dc_x] + 1;
        if (yl > yh) continue;
        int64_t frac = (int64_t)(dc_texturemid - (post->topdelta << FRACBITS)) + (int64_t)(yl - centery) * dc_iscale;
        int64_t last = frac + (int64_t)(yh - yl) * dc_iscale, end = (int64_t)post->length * FRACUNIT;
        if (frac < 0 || last < 0 || frac >= end || last >= end) return 1;
    }
    return 0;
}

int main(void)
{
    static byte patch[8][1024], block[8 * 256], scratch[264];
    for (int l = 0; l < 64; l++) for (int c = 0; c < 256; c++) maps[l][c] = (byte)(c * 7 + l * 13);
    for (int y = 0; y < SCREENHEIGHT; y++) ylookup[y] = screen[0] + y * SCREENWIDTH;
    for (int x = 0; x < SCREENWIDTH; x++) columnofs[x] = x;
    for (int trial = 0; trial < 400000; trial++) {
        int height = (int[]){16, 64, 72, 128, 136, 200, 256}[rnd() % 7];
        int composite = rnd() % 3 == 0;
        column_t *col;
        byte *bcol = block;
        /* One texture column: random posts (non-overlapping, inside height)
         * or a composite column (one full post, length capped at 254). */
        memset(bcol, 0, 256);
        if (composite) {
            int h = height > 254 ? 254 : height;
            for (int y = 0; y < height; y++) bcol[y] = (byte)rnd();
            scratch[0] = 0; scratch[1] = (byte)h; scratch[2] = 0;
            memcpy(scratch + 3, bcol, h); scratch[3 + h] = 0; scratch[4 + h] = 0xff;
            col = (column_t *)scratch;
        } else {
            byte *p = patch[trial & 7];
            int y = rnd() % 8, at = 0;
            while (y < height && at < 900) {
                int len = 1 + rnd() % (height - y < 200 ? height - y : 200);
                p[at] = (byte)y; p[at + 1] = (byte)len; p[at + 2] = 0;
                for (int i = 0; i < len; i++) bcol[y + i] = p[at + 3 + i] = (byte)rnd();
                p[at + 3 + len] = (byte)rnd();       /* pad: never sampled */
                at += len + 4;
                y += len + rnd() % 12;
            }
            p[at] = 0xff;
            col = (column_t *)p;
        }
        viewheight = trial % 5 ? 168 : 200;
        centery = viewheight / 2;
        dc_x = rnd() % SCREENWIDTH;
        /* Every fourth column is searched for one whose posts reach rows the
         * CPU walk clamps (rare reciprocal roundings at post boundaries). */
        for (int tries = 0; tries < (trial % 4 == 3 ? 5000 : 1); tries++) {
            draw_params(trial, height);
            if (clamps(col)) break;
        }
        maskedcolormaprow = trial % 23 ? (int)(rnd() % 64) : -1;
        dc_colormap = maskedcolormaprow >= 0 ? maps[maskedcolormaprow] : maps[0];
        detailshift = trial % 29 == 0;
        gpu_ok = trial % 31 != 0;
        memset(screen, 0, sizeof(screen));
        /* CPU reference into screen[0]. */
        R_DrawMaskedColumn(col);
        /* Column-block path into screen[1]; a declined column is walked on
         * the CPU there too, as R_RenderMaskedSegRange does. */
        if (!R_DrawMaskedColumnGPU(col, bcol)) {
            declined++;
            for (int y = 0; y < SCREENHEIGHT; y++) ylookup[y] = screen[1] + y * SCREENWIDTH;
            R_DrawMaskedColumn(col);
            for (int y = 0; y < SCREENHEIGHT; y++) ylookup[y] = screen[0] + y * SCREENWIDTH;
        }
        if (memcmp(screen[0], screen[1], sizeof(screen[0])) != 0) {
            fprintf(stderr, "FAIL trial %d: column %d differs (scale %d texturemid %d composite %d)\\n",
                    trial, dc_x, spryscale, dc_texturemid, composite);
            return 1;
        }
    }
    printf("PASS 400000 masked columns identical; %u GPU posts (%u with clamped rows, %u segments), "
           "%u columns declined to the CPU walk, %u CPU posts\n",
           gpu_columns, clamped_posts, gpu_segments, declined, cpu_columns);
    return gpu_columns > 100000 && clamped_posts > 1000 ? 0 : 2;
}
