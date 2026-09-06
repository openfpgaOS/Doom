/* Verify map-dependent spawned effects without loading every monster. */
#include <assert.h>
#include <stdio.h>
#ifndef DOOM_DATA_SOURCE
#define DOOM_DATA_SOURCE "../../src/doom/cdoom/doom/r_data.c"
#endif
#include DOOM_DATA_SOURCE
int main(void)
{
    byte present[NUMSPRITES] = {0};
    R_MarkSpawnedSprites(present);
    for (int i = 0; i < NUMSPRITES; ++i) assert(!present[i]);
    present[SPR_TROO] = present[SPR_BAR1] = 1;
    R_MarkSpawnedSprites(present);
    assert(present[SPR_BAL1] && present[SPR_BEXP]);
    assert(!present[SPR_FIRE] && !present[SPR_APLS] && !present[SPR_SKUL]);
    present[SPR_SKEL] = present[SPR_FATT] = present[SPR_BSPI] = 1;
    present[SPR_VILE] = present[SPR_PAIN] = present[SPR_HEAD] = 1;
    present[SPR_BOSS] = present[SPR_BBRN] = 1;
    R_MarkSpawnedSprites(present);
    const spritenum_t required[] = {SPR_FATB, SPR_FBXP, SPR_MANF, SPR_APLS,
        SPR_APBX, SPR_FIRE, SPR_SKUL, SPR_BAL2, SPR_BAL7, SPR_BOSF};
    for (unsigned i = 0; i < sizeof(required) / sizeof(required[0]); ++i)
        assert(present[required[i]]);
    assert(!present[SPR_CYBR] && !present[SPR_SPID]);
    puts("PASS: map-dependent monster projectiles and barrel explosions");
}
