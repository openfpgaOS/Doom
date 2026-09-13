/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Compare plane selection, merging, splitting and every readable column. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define OF_FASTRAM_H
#define OF_FASTTEXT __attribute__((noinline))
#include PLANE_SOURCE

int skyflatnum = 7;
void I_Error(const char *error, ...) { (void)error; abort(); }
static uint32_t seed = 0x7482abcf;
static uint32_t random_u32(void)
{
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return seed;
}
static void write_u32(uint32_t v) { assert(fwrite(&v, sizeof(v), 1, stdout) == 1); }
int main(void)
{
    memset(visplanes, 0xa5, sizeof(visplanes));
    for (int frame = 0; frame < 1024; frame++) {
        lastvisplane = visplanes;
        memset(visplane_hash, 0, sizeof(visplane_hash));
        for (int call = 0; call < 128; call++) {
            uint32_t r = random_u32();
            int pic = r & 7, height = (r >> 8) & 7;
            visplane_t *pl = R_FindPlane(height * FRACUNIT, pic, 160);
            int start = (r >> 16) % SCREENWIDTH;
            int stop = start + (r >> 24) % (SCREENWIDTH - start);
            if (call % 31 == 0) { start = 0; stop = SCREENWIDTH - 1; }
            if (call % 17 == 0) stop = start;
            pl = R_CheckPlane(pl, start, stop);
            write_u32(pl - visplanes);
            for (int x = start; x <= stop; x++) {
                r = random_u32();
                if (r & 3) {
                    int top = r % SCREENHEIGHT;
                    pl->top[x] = top;
                    pl->bottom[x] = top + (r >> 16) % (SCREENHEIGHT - top);
                }
            }
        }
        write_u32(lastvisplane - visplanes);
        for (visplane_t *pl = visplanes; pl < lastvisplane; pl++) {
            write_u32(pl->minx); write_u32(pl->maxx);
            if (pl->minx > pl->maxx) continue;
            for (int x = pl->minx; x <= pl->maxx; x++) {
                write_u32(pl->top[x]);
                if (pl->top[x] != 255) write_u32(pl->bottom[x]);
            }
        }
    }
    return 0;
}
