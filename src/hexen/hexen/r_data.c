//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 1993-2008 Raven Software
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//


#include "h2def.h"
#include "i_system.h"
#include "i_swap.h"
#include "m_misc.h"
#include "r_local.h"
#include "r_gpu.h"
#include "p_local.h"

typedef struct
{
    int originx;                // block origin (allways UL), which has allready
    int originy;                // accounted  for the patch's internal origin
    int patch;
} texpatch_t;

// a maptexturedef_t describes a rectangular texture, which is composed of one
// or more mappatch_t structures that arrange graphic patches
typedef struct
{
    char name[8];               // for switch changing, etc
    short width;
    short height;
    short patchcount;
    texpatch_t patches[1];      // [patchcount] drawn back to front
    //  into the cached texture
} texture_t;



int firstflat, lastflat, numflats;
int firstpatch, lastpatch, numpatches;
int firstspritelump, lastspritelump, numspritelumps;

int numtextures;
texture_t **textures;
int *texturewidthmask;
fixed_t *textureheight;         // needed for texture pegging
int *texturecompositesize;
short **texturecolumnlump;
unsigned short **texturecolumnofs;
byte **texturecomposite;

int *flattranslation;           // for global animation
int *texturetranslation;        // for global animation

fixed_t *spritewidth;           // needed for pre rendering
fixed_t *spriteoffset;
fixed_t *spritetopoffset;

lighttable_t *colormaps;


/*
==============================================================================

						MAPTEXTURE_T CACHING

when a texture is first needed, it counts the number of composite columns
required in the texture and allocates space for a column directory and any
new columns.  The directory will simply point inside other patches if there
is only one patch in a given column, but any columns with multiple patches
will have new column_ts generated.

==============================================================================
*/

/*
===================
=
= R_DrawColumnInCache
=
= Clip and draw a column from a patch into a cached post
=
===================
*/

void R_DrawColumnInCache(column_t * patch, byte * cache, int originy,
                         int cacheheight)
{
    int count, position;
    byte *source;

    while (patch->topdelta != 0xff)
    {
        source = (byte *) patch + 3;
        count = patch->length;
        position = originy + patch->topdelta;
        if (position < 0)
        {
            count += position;
            position = 0;
        }
        if (position + count > cacheheight)
            count = cacheheight - position;
        if (count > 0)
            memcpy(cache + position, source, count);

        patch = (column_t *) ((byte *) patch + patch->length + 4);
    }
}


/*
===================
=
= R_GenerateComposite
=
===================
*/

void R_GenerateComposite(int texnum)
{
    byte *block;
    texture_t *texture;
    texpatch_t *patch;
    patch_t *realpatch;
    int x, x1, x2;
    int i;
    column_t *patchcol;
    short *collump;
    unsigned short *colofs;

    texture = textures[texnum];
    block = Z_Malloc(texturecompositesize[texnum], PU_STATIC,
                     &texturecomposite[texnum]);
    collump = texturecolumnlump[texnum];
    colofs = texturecolumnofs[texnum];

//
// composite the columns together
//
    for (i = 0, patch = texture->patches; i < texture->patchcount;
         i++, patch++)
    {
        realpatch = W_CacheLumpNum(patch->patch, PU_CACHE);
        x1 = patch->originx;
        x2 = x1 + SHORT(realpatch->width);

        if (x1 < 0)
            x = 0;
        else
            x = x1;
        if (x2 > texture->width)
            x2 = texture->width;

        for (; x < x2; x++)
        {
            if (collump[x] >= 0)
                continue;       // column does not have multiple patches
            patchcol = (column_t *) ((byte *) realpatch +
                                     LONG(realpatch->columnofs[x - x1]));
            R_DrawColumnInCache(patchcol, block + colofs[x], patch->originy,
                                texture->height);
        }

    }

// The composite was built by CPU writes: flush it for GPU texel
// fetches.  Keep it PU_LEVEL — staged GPU commands reference it and a
// PU_CACHE purge could reuse the memory under them (Doom core policy).
    R_GPU_TextureDataUpdated(block, texturecompositesize[texnum]);
    Z_ChangeTag(block, PU_LEVEL);
}


/*
===================
=
= R_GenerateLookup
=
===================
*/

void R_GenerateLookup(int texnum)
{
    texture_t *texture;
    byte *patchcount;           // [texture->width]
    texpatch_t *patch;
    patch_t *realpatch;
    int x, x1, x2;
    int i;
    short *collump;
    unsigned short *colofs;

    texture = textures[texnum];

    texturecomposite[texnum] = 0;       // composited not created yet
    texturecompositesize[texnum] = 0;
    collump = texturecolumnlump[texnum];
    colofs = texturecolumnofs[texnum];

//
// count the number of columns that are covered by more than one patch
// fill in the lump / offset, so columns with only a single patch are
// all done
//
    patchcount = (byte *) Z_Malloc(texture->width, PU_STATIC, &patchcount);
    memset(patchcount, 0, texture->width);
    for (i = 0, patch = texture->patches; i < texture->patchcount;
         i++, patch++)
    {
        realpatch = W_CacheLumpNum(patch->patch, PU_CACHE);
        x1 = patch->originx;
        x2 = x1 + SHORT(realpatch->width);
        if (x1 < 0)
            x = 0;
        else
            x = x1;
        if (x2 > texture->width)
            x2 = texture->width;
        for (; x < x2; x++)
        {
            patchcount[x]++;
            collump[x] = patch->patch;
            colofs[x] = LONG(realpatch->columnofs[x - x1]) + 3;
        }
    }

    for (x = 0; x < texture->width; x++)
    {
        if (!patchcount[x])
        {
            ST_Message("R_GenerateLookup: column without a patch (%s)\n",
                       texture->name);
            return;
        }
//                      I_Error ("R_GenerateLookup: column without a patch");
        if (patchcount[x] > 1)
        {
            collump[x] = -1;    // use the cached block
            colofs[x] = texturecompositesize[texnum];
            if (texturecompositesize[texnum] > 0x10000 - texture->height)
                I_Error("R_GenerateLookup: texture %i is >64k", texnum);
            texturecompositesize[texnum] += texture->height;
        }
    }

    Z_Free(patchcount);
}


/*
================
=
= R_GetColumn
=
================
*/

byte *R_GetColumn(int tex, int col)
{
    int lump, ofs;

    col &= texturewidthmask[tex];
    lump = texturecolumnlump[tex][col];
    ofs = texturecolumnofs[tex][col];
    if (lump > 0)
        return (byte *) W_CacheLumpNum(lump, PU_LEVEL) + ofs;
    if (!texturecomposite[tex])
        R_GenerateComposite(tex);
    return texturecomposite[tex] + ofs;
}

/* ================================================================
 * Flat 2D wall-texture cache for the GPU param-wall path (ported from
 * the Doom core).  Column x lives at block + x*texheight; memcpy of
 * texheight bytes per column reads exactly the byte range R_DrawColumn
 * would read, so short-post garbage matches software byte for byte.
 * Blocks are PU_LEVEL with an owner pointer (auto-NULLed on level
 * free); a per-level byte budget caps zone pressure - over budget,
 * callers fall back to column emission.
 * ================================================================ */
#define GPU_WALL_TEX2D_BUDGET (2 * 1024 * 1024)
#define GPU_WALL_TEX2D_PAD 128

static byte **gpu_wall_tex2d;
static int gpu_wall_tex2d_budget;
static byte **gpu_masked_tex2d;

byte *R_GetWallTexture2D(int texnum)
{
    texture_t *texture;
    byte *block;
    int width;
    int height;
    int size;
    int x;

    if (gpu_wall_tex2d == NULL)
        return NULL;
    if (gpu_wall_tex2d[texnum] != NULL)
        return gpu_wall_tex2d[texnum];

    texture = textures[texnum];
    width = texturewidthmask[texnum] + 1;
    if (width > texture->width)
        width = texture->width;
    height = texture->height;
    size = width * height + GPU_WALL_TEX2D_PAD;

    if (size > gpu_wall_tex2d_budget)
        return NULL;

    block = Z_Malloc(size, PU_LEVEL, &gpu_wall_tex2d[texnum]);
    gpu_wall_tex2d_budget -= size;

    for (x = 0; x < width; x++)
        memcpy(block + x * height, R_GetColumn(texnum, x), height);
    memset(block + width * height, 0, GPU_WALL_TEX2D_PAD);

    R_GPU_TextureDataUpdated(block, (unsigned int)size);

    return block;
}


/* Post-aware 2D block for the GPU param-masked path: unlike the wall
 * cache (linear memcpy - post offsets collapse), this decodes column
 * posts at their topdelta rows like the sprite atlas.  Restricted to
 * textures whose columns are all patch-backed (multipatch masked =
 * vanilla medusa - those keep the column fallback) and height <= 128
 * (the tier path's vtex &127 wrap).  Shares the wall cache budget. */
byte *R_GetMaskedTexture2D(int texnum)
{
    texture_t *texture;
    byte *block;
    int width;
    int height;
    int size;
    int x;

    if (gpu_masked_tex2d == NULL)
        return NULL;
    if (gpu_masked_tex2d[texnum] != NULL)
        return gpu_masked_tex2d[texnum];

    texture = textures[texnum];
    width = texturewidthmask[texnum] + 1;
    if (width > texture->width)
        width = texture->width;
    height = texture->height;
    if (height <= 0 || height > 128)
        return NULL;

    for (x = 0; x < width; x++)
        if (texturecolumnlump[texnum][x] <= 0)
            return NULL;

    size = width * height + GPU_WALL_TEX2D_PAD;
    if (size > gpu_wall_tex2d_budget)
        return NULL;

    block = Z_Malloc(size, PU_LEVEL, &gpu_masked_tex2d[texnum]);
    gpu_wall_tex2d_budget -= size;
    memset(block, 0, size);

    for (x = 0; x < width; x++)
    {
        const column_t *column = (const column_t *)
            (R_GetColumn(texnum, x) - 3);

        while (column->topdelta != 0xff)
        {
            int len = column->length;
            int top = column->topdelta;

            if (top + len > height)
                len = height - top;
            if (len > 0)
                memcpy(block + x * height + top,
                       (const byte *)column + 3, len);
            column = (const column_t *)
                ((const byte *)column + column->length + 4);
        }
    }

    R_GPU_TextureDataUpdated(block, (unsigned int)size);

    return block;
}

/* Flat 2D sprite block for the GPU affine-sprite path (ported from the
 * Doom core): patch posts decoded column-major (column x at
 * block + x*height, transparent texels zero).  Records cover only post
 * extents, so the zeros are never sampled except at clamped boundary
 * roundings. */
#define GPU_SPRITE_TEX2D_BUDGET (2 * 1024 * 1024)

static byte **gpu_sprite_tex2d;
static int gpu_sprite_tex2d_budget;

byte *R_GetSpriteTexture2D(int spritelump)
{
    patch_t *patch;
    byte *block;
    int width;
    int height;
    int size;
    int x;

    if (gpu_sprite_tex2d == NULL)
        return NULL;
    if (spritelump < 0 || spritelump >= numspritelumps)
        return NULL;
    if (gpu_sprite_tex2d[spritelump] != NULL)
        return gpu_sprite_tex2d[spritelump];

    patch = W_CacheLumpNum(firstspritelump + spritelump, PU_LEVEL);
    width = SHORT(patch->width);
    height = SHORT(patch->height);
    if (width <= 0 || height <= 0 || height > 0xFFFF)
        return NULL;
    size = width * height;

    if (size > gpu_sprite_tex2d_budget)
        return NULL;

    block = Z_Malloc(size, PU_LEVEL, &gpu_sprite_tex2d[spritelump]);
    gpu_sprite_tex2d_budget -= size;
    memset(block, 0, size);

    for (x = 0; x < width; x++)
    {
        const column_t *column = (const column_t *)
            ((const byte *)patch + LONG(patch->columnofs[x]));

        while (column->topdelta != 0xff)
        {
            int len = column->length;
            int top = column->topdelta;

            if (top + len > height)
                len = height - top;
            if (len > 0)
                memcpy(block + x * height + top,
                       (const byte *)column + 3, len);
            column = (const column_t *)
                ((const byte *)column + column->length + 4);
        }
    }

    R_GPU_TextureDataUpdated(block, (unsigned int)size);

    return block;
}



/*
==================
=
= R_InitTextures
=
= Initializes the texture list with the textures from the world map
=
==================
*/

void R_InitTextures(void)
{
    maptexture_t *mtexture;
    texture_t *texture;
    mappatch_t *mpatch;
    texpatch_t *patch;
    int i, j;
    int *maptex, *maptex2, *maptex1;
    char name[9], *names, *name_p;
    int *patchlookup;
    int nummappatches;
    int offset, maxoff, maxoff2;
    int numtextures1, numtextures2;
    int *directory;

//
// load the patch names from pnames.lmp
//
    names = W_CacheLumpName("PNAMES", PU_STATIC);
    nummappatches = LONG(*((int *) names));
    name_p = names + 4;
    patchlookup = Z_Malloc(nummappatches * sizeof(*patchlookup), PU_STATIC, NULL);
    for (i = 0; i < nummappatches; i++)
    {
        M_StringCopy(name, name_p + i * 8, sizeof(name));
        patchlookup[i] = W_CheckNumForName(name);
    }
    W_ReleaseLumpName("PNAMES");

//
// load the map texture definitions from textures.lmp
//
    maptex = maptex1 = W_CacheLumpName("TEXTURE1", PU_STATIC);
    numtextures1 = LONG(*maptex);
    maxoff = W_LumpLength(W_GetNumForName("TEXTURE1"));
    directory = maptex + 1;

    if (W_CheckNumForName("TEXTURE2") != -1)
    {
        maptex2 = W_CacheLumpName("TEXTURE2", PU_STATIC);
        numtextures2 = LONG(*maptex2);
        maxoff2 = W_LumpLength(W_GetNumForName("TEXTURE2"));
    }
    else
    {
        maptex2 = NULL;
        numtextures2 = 0;
        maxoff2 = 0;
    }
    numtextures = numtextures1 + numtextures2;

    textures = Z_Malloc(numtextures * sizeof(texture_t *), PU_STATIC, 0);
    texturecolumnlump = Z_Malloc(numtextures * sizeof(short *), PU_STATIC, 0);
    texturecolumnofs = Z_Malloc(numtextures * sizeof(short *), PU_STATIC, 0);
    texturecomposite = Z_Malloc(numtextures * sizeof(byte *), PU_STATIC, 0);
    texturecompositesize = Z_Malloc(numtextures * sizeof(int), PU_STATIC, 0);
    texturewidthmask = Z_Malloc(numtextures * sizeof(int), PU_STATIC, 0);
    gpu_wall_tex2d = Z_Malloc(numtextures * sizeof(*gpu_wall_tex2d), PU_STATIC, 0);
    memset(gpu_wall_tex2d, 0, numtextures * sizeof(*gpu_wall_tex2d));
    gpu_wall_tex2d_budget = GPU_WALL_TEX2D_BUDGET;
    gpu_masked_tex2d = Z_Malloc(numtextures * sizeof(*gpu_masked_tex2d), PU_STATIC, 0);
    memset(gpu_masked_tex2d, 0, numtextures * sizeof(*gpu_masked_tex2d));
    textureheight = Z_Malloc(numtextures * sizeof(fixed_t), PU_STATIC, 0);

    for (i = 0; i < numtextures; i++, directory++)
    {
        if (i == numtextures1)
        {                       // start looking in second texture file
            maptex = maptex2;
            maxoff = maxoff2;
            directory = maptex + 1;
        }

        offset = LONG(*directory);
        if (offset > maxoff)
            I_Error("R_InitTextures: bad texture directory");
        mtexture = (maptexture_t *) ((byte *) maptex + offset);
        texture = textures[i] = Z_Malloc(sizeof(texture_t)
                                         +
                                         sizeof(texpatch_t) *
                                         (SHORT(mtexture->patchcount) - 1),
                                         PU_STATIC, 0);
        texture->width = SHORT(mtexture->width);
        texture->height = SHORT(mtexture->height);
        texture->patchcount = SHORT(mtexture->patchcount);
        memcpy(texture->name, mtexture->name, sizeof(texture->name));
        mpatch = &mtexture->patches[0];
        patch = &texture->patches[0];
        for (j = 0; j < texture->patchcount; j++, mpatch++, patch++)
        {
            patch->originx = SHORT(mpatch->originx);
            patch->originy = SHORT(mpatch->originy);
            patch->patch = patchlookup[SHORT(mpatch->patch)];
            if (patch->patch == -1)
                I_Error("R_InitTextures: Missing patch in texture %s",
                        texture->name);
        }
        texturecolumnlump[i] = Z_Malloc(texture->width * sizeof(short), PU_STATIC, 0);
        texturecolumnofs[i] = Z_Malloc(texture->width * sizeof(short), PU_STATIC, 0);
        j = 1;
        while (j * 2 <= texture->width)
            j <<= 1;
        texturewidthmask[i] = j - 1;
        textureheight[i] = texture->height << FRACBITS;
    }

    Z_Free(patchlookup);

    W_ReleaseLumpName("TEXTURE1");
    if (maptex2)
        W_ReleaseLumpName("TEXTURE2");

//
// precalculate whatever possible
//              
    for (i = 0; i < numtextures; i++)
    {
        R_GenerateLookup(i);
        if (!(i & 31))
            ST_Progress();
    }

//
// translation table for global animation
//
    texturetranslation = Z_Malloc((numtextures + 1) * sizeof(int), PU_STATIC, 0);
    for (i = 0; i < numtextures; i++)
        texturetranslation[i] = i;
}


/*
================
=
= R_InitFlats
=
=================
*/

void R_InitFlats(void)
{
    int i;

    firstflat = W_GetNumForName("F_START") + 1;
    lastflat = W_GetNumForName("F_END") - 1;
    numflats = lastflat - firstflat + 1;

// translation table for global animation
    flattranslation = Z_Malloc((numflats + 1) * sizeof(int), PU_STATIC, 0);
    for (i = 0; i < numflats; i++)
        flattranslation[i] = i;
}


/*
================
=
= R_InitSpriteLumps
=
= Finds the width and hoffset of all sprites in the wad, so the sprite doesn't
= need to be cached just for the header during rendering
=================
*/

void R_InitSpriteLumps(void)
{
    int i;
    patch_t *patch;

    firstspritelump = W_GetNumForName("S_START") + 1;
    lastspritelump = W_GetNumForName("S_END") - 1;
    numspritelumps = lastspritelump - firstspritelump + 1;
    spritewidth = Z_Malloc(numspritelumps * sizeof(fixed_t), PU_STATIC, 0);
    spriteoffset = Z_Malloc(numspritelumps * sizeof(fixed_t), PU_STATIC, 0);
    spritetopoffset = Z_Malloc(numspritelumps * sizeof(fixed_t), PU_STATIC, 0);
    gpu_sprite_tex2d = Z_Malloc(numspritelumps * sizeof(*gpu_sprite_tex2d), PU_STATIC, 0);
    memset(gpu_sprite_tex2d, 0, numspritelumps * sizeof(*gpu_sprite_tex2d));
    gpu_sprite_tex2d_budget = GPU_SPRITE_TEX2D_BUDGET;

    for (i = 0; i < numspritelumps; i++)
    {
        if (!(i & 127))
            ST_Progress();
        patch = W_CacheLumpNum(firstspritelump + i, PU_CACHE);
        spritewidth[i] = SHORT(patch->width) << FRACBITS;
        spriteoffset[i] = SHORT(patch->leftoffset) << FRACBITS;
        spritetopoffset[i] = SHORT(patch->topoffset) << FRACBITS;
    }
}


/*
================
=
= R_InitColormaps
=
=================
*/

void R_InitColormaps(void)
{
    int lump, length;
//
// load in the light tables
// 256 byte align tables
//
    lump = W_GetNumForName("COLORMAP");
    length = W_LumpLength(lump);
    colormaps = Z_Malloc(length, PU_STATIC, 0);
    W_ReadLump(lump, colormaps);
}


/*
================
=
= R_InitData
=
= Locates all the lumps that will be used by all views
= Must be called after W_Init
=================
*/

void R_InitData(void)
{
    R_InitTextures();
    R_InitFlats();
    R_InitSpriteLumps();
    R_InitColormaps();
}

//=============================================================================

/*
================
=
= R_FlatNumForName
=
================
*/

int R_FlatNumForName(const char *name)
{
    int i;
    char namet[9];

    i = W_CheckNumForName(name);
    if (i == -1)
    {
        namet[8] = 0;
        memcpy(namet, name, 8);
        I_Error("R_FlatNumForName: %s not found", namet);
    }
    return i - firstflat;
}


/*
================
=
= R_CheckTextureNumForName
=
================
*/

int R_CheckTextureNumForName(const char *name)
{
    int i;

    if (name[0] == '-')         // no texture marker
        return 0;

    for (i = 0; i < numtextures; i++)
        if (!strncasecmp(textures[i]->name, name, 8))
            return i;

    return -1;
}


/*
================
=
= R_TextureNumForName
=
================
*/

int R_TextureNumForName(const char *name)
{
    int i;
    //char  namet[9];

    i = R_CheckTextureNumForName(name);
    if (i == -1)
        I_Error("R_TextureNumForName: %s not found", name);

    return i;
}


/*
=================
=
= R_PrecacheLevel
=
= Preloads all relevent graphics for the level
=================
*/

int flatmemory, texturememory, spritememory;

void R_PrecacheLevel(void)
{
    /* PU_LEVEL frees auto-NULLed the owner pointers; the 2D texture
     * cache restarts from a full budget each level (openfpgaOS). */
    gpu_wall_tex2d_budget = GPU_WALL_TEX2D_BUDGET;
    gpu_sprite_tex2d_budget = GPU_SPRITE_TEX2D_BUDGET;

    char *flatpresent;
    char *texturepresent;
    char *spritepresent;
    int i, j, k, lump;
    texture_t *texture;
    thinker_t *th;
    spriteframe_t *sf;

    if (demoplayback)
        return;

//
// precache flats
//      
    flatpresent = Z_Malloc(numflats, PU_STATIC, NULL);
    memset(flatpresent, 0, numflats);
    for (i = 0; i < numsectors; i++)
    {
        flatpresent[sectors[i].floorpic] = 1;
        flatpresent[sectors[i].ceilingpic] = 1;
    }

    flatmemory = 0;
    for (i = 0; i < numflats; i++)
        if (flatpresent[i])
        {
            lump = firstflat + i;
            flatmemory += lumpinfo[lump]->size;
            W_CacheLumpNum(lump, PU_CACHE);
        }

    Z_Free(flatpresent);

//
// precache textures
//
    texturepresent = Z_Malloc(numtextures, PU_STATIC, NULL);
    memset(texturepresent, 0, numtextures);

    for (i = 0; i < numsides; i++)
    {
        texturepresent[sides[i].toptexture] = 1;
        texturepresent[sides[i].midtexture] = 1;
        texturepresent[sides[i].bottomtexture] = 1;
    }

    texturepresent[Sky1Texture] = 1;
    texturepresent[Sky2Texture] = 1;

    texturememory = 0;
    for (i = 0; i < numtextures; i++)
    {
        if (!texturepresent[i])
            continue;
        texture = textures[i];
        for (j = 0; j < texture->patchcount; j++)
        {
            lump = texture->patches[j].patch;
            texturememory += lumpinfo[lump]->size;
            W_CacheLumpNum(lump, PU_CACHE);
        }

        /* Prebuild the param-wall 2D block now: a lazy first-sighting
         * build costs a full GPU pipeline drain mid-frame (openfpgaOS). */
        R_GetWallTexture2D(i);
    }

    Z_Free(texturepresent);

//
// precache sprites
//
    spritepresent = Z_Malloc(numsprites, PU_STATIC, NULL);
    memset(spritepresent, 0, numsprites);

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
        if (th->function == P_MobjThinker)
            spritepresent[((mobj_t *) th)->sprite] = 1;
    }

    spritememory = 0;
    for (i = 0; i < numsprites; i++)
    {
        if (!spritepresent[i])
            continue;
        for (j = 0; j < sprites[i].numframes; j++)
        {
            sf = &sprites[i].spriteframes[j];
            for (k = 0; k < 8; k++)
            {
                lump = firstspritelump + sf->lump[k];
                spritememory += lumpinfo[lump]->size;
                W_CacheLumpNum(lump, PU_CACHE);
                /* Prebuild the affine-sprite block (openfpgaOS). */
                R_GetSpriteTexture2D(sf->lump[k]);
            }
        }
    }

    Z_Free(spritepresent);
}
