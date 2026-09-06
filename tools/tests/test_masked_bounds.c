/* The BON1 top-edge rounding case must never wrap a negative index to 127. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef DOOM_DRAW_SOURCE
#define DOOM_DRAW_SOURCE "../../src/doom/cdoom/doom/r_draw.c"
#endif
#include DOOM_DRAW_SOURCE
int centery;
void (*colfunc)(void);
boolean R_GPU_DrawColumn(void) { return false; }
void R_GPU_PrepareForCPUAccessRect(int x, int y, int w, int h)
{ assert(x >= 0 && x + w <= SCREENWIDTH && y >= 0 && y + h <= SCREENHEIGHT); }

int main(void)
{
    static byte buffer[SCREENWIDTH * SCREENHEIGHT], colors[256], translation[256];
    for (int i = 0; i < 256; ++i) { colors[i] = 255 - i; translation[i] = i ^ 73; }
    for (int y = 0; y < SCREENHEIGHT; ++y) ylookup[y] = buffer + y * SCREENWIDTH;
    for (int x = 0; x < SCREENWIDTH; ++x) columnofs[x] = x;
    dc_colormap = colors; dc_translation = translation;
    void (*drawers[])(void) = {R_DrawColumn, R_DrawColumnLow,
                              R_DrawTranslatedColumn, R_DrawTranslatedColumnLow};
    unsigned cases = 0;
    const int offsets[] = {-FRACUNIT - 1, -2, -1, 0, 1, 2, FRACUNIT + 1};
    for (int length = 1; length <= 255; ++length) {
        byte *post = malloc(length); /* ASan redzones expose any out-of-post read. */
        for (int i = 0; i < length; ++i) post[i] = i;
        dc_source = post;
        for (int mode = 0; mode < 4; ++mode)
            for (unsigned o = 0; o < sizeof(offsets) / sizeof(offsets[0]); ++o) {
                int offset = offsets[o];
                colfunc = drawers[mode];
                dc_x = 5; dc_yl = 0; dc_yh = length < SCREENHEIGHT ? length - 1 : SCREENHEIGHT - 1;
                dc_iscale = FRACUNIT;
                dc_texturemid = offset;
                memset(buffer, 17, sizeof(buffer));
                if (!R_DrawClampedMaskedColumn(length)) colfunc();
                for (int y = dc_yl; y <= dc_yh; ++y) {
                    int sample = (offset + y * FRACUNIT) >> FRACBITS;
                    if (sample < 0) sample = 0;
                    if (sample >= length) sample = length - 1;
                    if (mode < 2) sample &= 127;
                    byte expected = colors[mode >= 2 ? translation[post[sample]] : post[sample]];
                    int x = dc_x << (mode & 1);
                    assert(buffer[y * SCREENWIDTH + x] == expected);
                    if (mode & 1) assert(buffer[y * SCREENWIDTH + x + 1] == expected);
                    assert(buffer[y * SCREENWIDTH + x - 1] == 17);
                }
                ++cases;
            }
        free(post);
    }
    printf("PASS: %u masked-post bounds cases, normal/translated and high/low detail\n", cases);
}
