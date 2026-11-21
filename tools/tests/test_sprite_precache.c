/* Verify map-dependent spawned effects without loading every monster. */
#include <assert.h>
#include <stdio.h>
#ifndef DOOM_DATA_SOURCE
#define DOOM_DATA_SOURCE "../../src/doom/cdoom/doom/r_data.c"
#endif
#include DOOM_DATA_SOURCE
/* Only the spawn states of the dropped-item table are needed; the values
 * are the ones in info.c. */
state_t states[NUMSTATES];
mobjinfo_t mobjinfo[NUMMOBJTYPES];
static void set_spawn(mobjtype_t type, statenum_t state, spritenum_t sprite)
{
    mobjinfo[type].spawnstate = state;
    states[state].sprite = sprite;
}
int main(void)
{
    byte present[NUMSPRITES] = {0};
    set_spawn(MT_POSSESSED, S_POSS_STND, SPR_POSS);
    set_spawn(MT_SHOTGUY, S_SPOS_STND, SPR_SPOS);
    set_spawn(MT_CHAINGUY, S_CPOS_STND, SPR_CPOS);
    set_spawn(MT_WOLFSS, S_SSWV_STND, SPR_SSWV);
    set_spawn(MT_CLIP, S_CLIP, SPR_CLIP);
    set_spawn(MT_SHOTGUN, S_SHOT, SPR_SHOT);
    set_spawn(MT_CHAINGUN, S_MGUN, SPR_MGUN);
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
    assert(!present[SPR_CLIP] && !present[SPR_SHOT] && !present[SPR_MGUN]);
    present[SPR_SPOS] = 1;
    R_MarkSpawnedSprites(present);
    assert(present[SPR_SHOT] && !present[SPR_CLIP] && !present[SPR_MGUN]);
    present[SPR_POSS] = present[SPR_CPOS] = 1;
    R_MarkSpawnedSprites(present);
    assert(present[SPR_CLIP] && present[SPR_MGUN]);
    puts("PASS: map-dependent monster projectiles and barrel explosions");
}
