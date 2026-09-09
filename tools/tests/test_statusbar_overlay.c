/* Exercise production status-bar redraws after menu overlays. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifndef DOOM_STATUS_SOURCE
#define DOOM_STATUS_SOURCE "../../src/doom/cdoom/doom/st_stuff.c"
#endif
#include DOOM_STATUS_SOURCE

#ifndef TEST_STATUS_INVALIDATE
#define ST_InvalidateBuffer() ((void)0)
#endif

boolean automapactive, netgame;
int deathmatch;
GameVersion_t gameversion = exe_doom_1_9;
static player_t test_player;
static byte buffers[3][SCREENWIDTH * SCREENHEIGHT];
static int draw_slot, direct_fb = 1, copies;

boolean R_GPU_UsingDirectFramebuffer(void) { return direct_fb; }
int R_GPU_CurrentDrawSlot(void) { return draw_slot; }
void V_UseBuffer(pixel_t *p) { (void)p; }
void V_RestoreBuffer(void) {}
void V_DrawPatch(int x, int y, patch_t *p) { (void)x; (void)y; (void)p; }
void V_CopyRect(int x, int y, pixel_t *src, int w, int h, int dx, int dy)
{
    (void)x; (void)y; (void)src;
    for (int row = dy; row < dy + h; ++row)
        memset(&buffers[draw_slot][row * SCREENWIDTH + dx], 0x42, w);
    ++copies;
}
void *W_CacheLumpNum(lumpindex_t n, int tag) { (void)n; (void)tag; return NULL; }
void I_SetPalette(byte *p) { (void)p; }
void STlib_updateNum(st_number_t *w, boolean r) { (void)w; (void)r; }
void STlib_updatePercent(st_percent_t *w, boolean r) { (void)w; (void)r; }
void STlib_updateBinIcon(st_binicon_t *w, boolean r) { (void)w; (void)r; }
void STlib_updateMultIcon(st_multicon_t *w, boolean r) { (void)w; (void)r; }

static void draw(int slot)
{
    draw_slot = slot;
    ST_Drawer(false, false);
    assert(buffers[slot][ST_Y * SCREENWIDTH + 100] == 0x42);
}

static void overlay(int slot)
{
    draw_slot = slot;
    ST_InvalidateBuffer();
    buffers[slot][ST_Y * SCREENWIDTH + 100] = 0xee;
}

int main(void)
{
    plyr = &test_player;
    st_firsttime = true;
    draw(0); draw(1); draw(2);
    int initial = copies;
    draw(0); draw(1); draw(2);
    assert(copies == initial);

    /* Options -> shorter parent menu, with no global menu-close edge. */
    overlay(2);
    for (int i = 0; i < 12; ++i) draw(i & 1);
    assert(copies == initial);
    draw(2);
    assert(copies == initial + 1);

    /* The automap path does not run D_Display's post-menu clear loop. */
    automapactive = true;
    overlay(0); overlay(1); overlay(2);
    draw(1); draw(1); draw(0); draw(2);
    assert(copies == initial + 4);

    direct_fb = 0;
    automapactive = false;
    draw_slot = 0;
    overlay(0);
    draw(0);
    assert(copies == initial + 5);
    puts("PASS: menu overlays, delayed/repeated framebuffer reuse, automap and software HUD");
    return 0;
}
