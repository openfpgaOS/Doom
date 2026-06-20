//
// Copyright(C) 1993-1996 Id Software, Inc.
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
// DESCRIPTION:
//	Preparation of data for rendering,
//	generation of lookups, caching, retrieval by name.
//

#include <stdio.h>

#include "deh_main.h"
#include "i_swap.h"
#include "i_system.h"
#include "z_zone.h"


#include "w_wad.h"

#include "doomdef.h"
#include "m_misc.h"
#include "r_gpu.h"
#include "r_local.h"
#include "p_local.h"

#include "doomstat.h"
#include "r_sky.h"


#include "r_data.h"

//
// Graphics.
// DOOM graphics for walls and sprites
// is stored in vertical runs of opaque pixels (posts).
// A column is composed of zero or more posts,
// a patch or sprite is composed of zero or more columns.
// 



//
// Texture definition.
// Each texture is composed of one or more patches,
// with patches being lumps stored in the WAD.
// The lumps are referenced by number, and patched
// into the rectangular texture space using origin
// and possibly other attributes.
//
typedef PACKED_STRUCT (
{
    short	originx;
    short	originy;
    short	patch;
    short	stepdir;
    short	colormap;
}) mappatch_t;


//
// Texture definition.
// A DOOM wall texture is a list of patches
// which are to be combined in a predefined order.
//
typedef PACKED_STRUCT (
{
    char		name[8];
    int			masked;	
    short		width;
    short		height;
    int                 obsolete;
    short		patchcount;
    mappatch_t	patches[1];
}) maptexture_t;


// A single patch from a texture definition,
//  basically a rectangular area within
//  the texture rectangle.
typedef struct
{
    // Block origin (allways UL),
    // which has allready accounted
    // for the internal origin of the patch.
    short	originx;	
    short	originy;
    int		patch;
} texpatch_t;


// A maptexturedef_t describes a rectangular texture,
//  which is composed of one or more mappatch_t structures
//  that arrange graphic patches.

typedef struct texture_s texture_t;

struct texture_s
{
    // Keep name for switch changing, etc.
    char	name[8];		
    short	width;
    short	height;

    // Index in textures list

    int         index;

    // Next in hash table chain

    texture_t  *next;
    
    // All the patches[patchcount]
    //  are drawn back to front into the cached texture.
    short	patchcount;
    texpatch_t	patches[1];		
};



int		firstflat;
int		lastflat;
int		numflats;
static byte**	flatlumpdata;

int		firstpatch;
int		lastpatch;
int		numpatches;

int		firstspritelump;
int		lastspritelump;
int		numspritelumps;

int		numtextures;
texture_t**	textures;
texture_t**     textures_hashtable;


int*			texturewidthmask;
// needed for texture pegging
fixed_t*		textureheight;		
int*			texturecompositesize;
short**			texturecolumnlump;
unsigned short**	texturecolumnofs;
byte**			texturecomposite;
static byte***		texturecolumnptr;

// for global animation
int*		flattranslation;
int*		texturetranslation;

// needed for pre rendering
fixed_t*	spritewidth;	
fixed_t*	spriteoffset;
fixed_t*	spritetopoffset;

lighttable_t	*colormaps;

static int ReadLE32Unaligned(const void *p)
{
    const byte *b = p;

    return (int) ((uint32_t) b[0]
               | ((uint32_t) b[1] << 8)
               | ((uint32_t) b[2] << 16)
               | ((uint32_t) b[3] << 24));
}


//
// MAPTEXTURE_T CACHING
// When a texture is first needed,
//  it counts the number of composite columns
//  required in the texture and allocates space
//  for a column directory and any new columns.
// The directory will simply point inside other patches
//  if there is only one patch in a given column,
//  but any columns with multiple patches
//  will have new column_ts generated.
//



//
// R_DrawColumnInCache
// Clip and draw a column
//  from a patch into a cached post.
//
void
R_DrawColumnInCache
( column_t*	patch,
  byte*		cache,
  int		originy,
  int		cacheheight )
{
    int		count;
    int		position;
    byte*	source;

    while (patch->topdelta != 0xff)
    {
	source = (byte *)patch + 3;
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
	    memcpy (cache + position, source, count);
		
	patch = (column_t *)(  (byte *)patch + patch->length + 4); 
    }
}



//
// R_GenerateComposite
// Using the texture definition,
//  the composite texture is created from the patches,
//  and each column is cached.
//
void R_GenerateComposite (int texnum)
{
    byte*		block;
    texture_t*		texture;
    texpatch_t*		patch;	
    patch_t*		realpatch;
    int			x;
    int			x1;
    int			x2;
    int			i;
    column_t*		patchcol;
    short*		collump;
    unsigned short*	colofs;
	
    texture = textures[texnum];

    block = Z_Malloc (texturecompositesize[texnum],
		      PU_STATIC, 
		      &texturecomposite[texnum]);	

    collump = texturecolumnlump[texnum];
    colofs = texturecolumnofs[texnum];
    
    // Composite the columns together.
    for (i=0 , patch = texture->patches;
	 i<texture->patchcount;
	 i++, patch++)
    {
	realpatch = W_CacheLumpNum (patch->patch, PU_CACHE);
	x1 = patch->originx;
	x2 = x1 + SHORT(realpatch->width);

	if (x1<0)
	    x = 0;
	else
	    x = x1;
	
	if (x2 > texture->width)
	    x2 = texture->width;

	for ( ; x<x2 ; x++)
	{
	    // Column does not have multiple patches?
	    if (collump[x] >= 0)
		continue;
	    
	    patchcol = (column_t *)((byte *)realpatch
				    + LONG(realpatch->columnofs[x-x1]));
	    R_DrawColumnInCache (patchcol,
				 block + colofs[x],
				 patch->originy,
				 texture->height);
	}
						
    }

    // The composite is referenced by raw pointers cached in texturecolumnptr[],
    //  so it must stay resident for the level.  A PU_CACHE composite can be
    //  purged out from under those pointers (dangling read) and, on the GPU
    //  build, regenerated with a full pipeline drain mid-frame -- the same
    //  reload-stall class that causes the rotation hiccup.  Keep it PU_LEVEL;
    //  it is reclaimed at the next level load.
    R_GPU_TextureDataUpdated(block, texturecompositesize[texnum]);
    Z_ChangeTag (block, PU_LEVEL);
}



//
// R_GenerateLookup
//
void R_GenerateLookup (int texnum)
{
    texture_t*		texture;
    byte*		patchcount;	// patchcount[texture->width]
    texpatch_t*		patch;	
    patch_t*		realpatch;
    int			x;
    int			x1;
    int			x2;
    int			i;
    short*		collump;
    unsigned short*	colofs;
	
    texture = textures[texnum];

    // Composited texture not created yet.
    texturecomposite[texnum] = 0;
    
    texturecompositesize[texnum] = 0;
    collump = texturecolumnlump[texnum];
    colofs = texturecolumnofs[texnum];
    
    // Now count the number of columns
    //  that are covered by more than one patch.
    // Fill in the lump / offset, so columns
    //  with only a single patch are all done.
    patchcount = (byte *) Z_Malloc(texture->width, PU_STATIC, &patchcount);
    memset (patchcount, 0, texture->width);

    for (i=0 , patch = texture->patches;
	 i<texture->patchcount;
	 i++, patch++)
    {
	realpatch = W_CacheLumpNum (patch->patch, PU_CACHE);
	x1 = patch->originx;
	x2 = x1 + SHORT(realpatch->width);
	
	if (x1 < 0)
	    x = 0;
	else
	    x = x1;

	if (x2 > texture->width)
	    x2 = texture->width;
	for ( ; x<x2 ; x++)
	{
	    patchcount[x]++;
	    collump[x] = patch->patch;
	    colofs[x] = LONG(realpatch->columnofs[x-x1])+3;
	}
    }
	
    for (x=0 ; x<texture->width ; x++)
    {
	if (!patchcount[x])
	{
	    printf ("R_GenerateLookup: column without a patch (%s)\n",
		    texture->name);
	    return;
	}
	// I_Error ("R_GenerateLookup: column without a patch");
	
	if (patchcount[x] > 1)
	{
	    // Use the cached block.
	    collump[x] = -1;	
	    colofs[x] = texturecompositesize[texnum];
	    
	    if (texturecompositesize[texnum] > 0x10000-texture->height)
	    {
		I_Error ("R_GenerateLookup: texture %i is >64k",
			 texnum);
	    }
	    
	    texturecompositesize[texnum] += texture->height;
	}
    }

    Z_Free(patchcount);
}




//
// R_GetColumn
//
static byte *R_CachedLumpData(lumpindex_t lump)
{
    lumpinfo_t *info = lumpinfo[lump];

    if (info->wad_file->mapped != NULL)
	return info->wad_file->mapped + info->position;

    if (info->cache != NULL)
	return info->cache;

    return W_CacheLumpNum(lump, PU_CACHE);
}

static void R_BuildTextureColumnPointers(int texnum)
{
    texture_t *texture = textures[texnum];
    byte **columns;

    if (texturecolumnptr[texnum] == NULL)
    {
        texturecolumnptr[texnum] =
            Z_Malloc(texture->width * sizeof(**texturecolumnptr),
                     PU_LEVEL, NULL);
    }

    columns = texturecolumnptr[texnum];

    for (int x = 0; x < texture->width; x++)
    {
        int lump = texturecolumnlump[texnum][x];
        int ofs = texturecolumnofs[texnum][x];

        if (lump > 0)
        {
            columns[x] = R_CachedLumpData(lump) + ofs;
        }
        else
        {
            if (!texturecomposite[texnum])
                R_GenerateComposite(texnum);

            columns[x] = texturecomposite[texnum] + ofs;
        }
    }
}

byte*
R_GetColumn
( int		tex,
  int		col )
{
    int		lump;
    int		ofs;
	
    col &= texturewidthmask[tex];

    if (texturecolumnptr[tex] != NULL)
	return texturecolumnptr[tex][col];

    lump = texturecolumnlump[tex][col];
    ofs = texturecolumnofs[tex][col];
    
    if (lump > 0)
	return R_CachedLumpData(lump)+ofs;

    if (!texturecomposite[tex])
	R_GenerateComposite (tex);

    return texturecomposite[tex] + ofs;
}

byte **R_GetColumnTable(int tex)
{
    if (texturecolumnptr[tex] == NULL)
        R_BuildTextureColumnPointers(tex);

    return texturecolumnptr[tex];
}

int R_GetTextureWidthMask(int tex)
{
    return texturewidthmask[tex];
}

/* ================================================================
 * Flat 2D wall-texture cache for the GPU param-wall path.
 *
 * The param-span command samples one tex_addr with (u,v) addressing,
 * but Doom textures are column-oriented with irregular column homes
 * (composite block for multipatched columns, patch lump + 3 for
 * single-patch columns).  This cache lays the columns the renderer
 * would actually read out as one contiguous column-major block:
 * column x at block + x*texheight.  memcpy of texheight bytes per
 * column reads exactly the byte range R_DrawColumn would read, so
 * short-post garbage (tutti-frutti) matches software byte for byte.
 *
 * Blocks are PU_LEVEL with an owner pointer (auto-NULLed on level
 * free, same lifetime as composites); a per-level byte budget caps
 * zone pressure — over budget, callers fall back to column emission.
 * ================================================================ */
/* Per-level byte budgets for the GPU 2D texel blocks.  These are zone-bound:
 * the block is built in the zone (it backs the upload + the CPU fallback), so
 * the cap protects the 32 MB zone, NOT the (much larger) fast texture tier.
 * Sized generously to use the fast tier -- a level only builds the textures it
 * actually uses, so the cap just removes the old 2 MB ceiling that was spilling
 * busy maps' overflow textures onto the CPU. */
#define GPU_WALL_TEX2D_BUDGET (9 * 1024 * 1024)
#define GPU_SPRITE_TEX2D_BUDGET (9 * 1024 * 1024)
/* Masked 2-sided midtextures are a small subset (a handful of grate/fence/window
 * textures per map); a modest cap covers them with room to spare. */
#define GPU_MASKED_TEX2D_BUDGET (2 * 1024 * 1024)
/* Once a level has built this many block bytes, guard each further block on
 * real zone headroom (Z_FreeMemory) so a texture-heavy map can never OOM the
 * unguarded composite/lump/gameplay allocations -- it just falls back to the
 * CPU instead.  The first GPU_TEX2D_GUARD_FLOOR bytes are always safe (the old
 * conservative budget), so typical maps pay no Z_FreeMemory cost. */
#define GPU_TEX2D_GUARD_FLOOR (2 * 1024 * 1024)
#define GPU_TEX2D_ZONE_RESERVE (8 * 1024 * 1024)
/* vtex wraps &127 like R_DrawColumn; for texheight < 128 the GPU can
 * read up to 127 bytes past the last column — pad so it stays inside
 * the block (software reads the same out-of-column bytes). */
#define GPU_WALL_TEX2D_PAD 128

static byte **gpu_wall_tex2d;
static int gpu_wall_tex2d_budget;
static byte **gpu_sprite_tex2d;
static int gpu_sprite_tex2d_budget;
static byte **gpu_masked_tex2d;
static int gpu_masked_tex2d_budget;
static int gpu_tex2d_dropped;    /* textures over budget -> CPU (diagnostic) */

/* True if building `size` more block bytes would leave the zone too tight for
 * the rest of precache + gameplay.  Only consults Z_FreeMemory past the safe
 * floor, so light maps stay fast to load. */
static boolean R_Tex2DZoneTight(int used, int size)
{
    return used >= GPU_TEX2D_GUARD_FLOOR
        && Z_FreeMemory() < size + GPU_TEX2D_ZONE_RESERVE;
}

byte *R_GetWallTexture2D(int texnum)
{
    texture_t *texture;
    byte *block;
    int width;
    int height;
    int size;

    if (gpu_wall_tex2d == NULL)
        return NULL;
    if (gpu_wall_tex2d[texnum] != NULL)
        return gpu_wall_tex2d[texnum];

    texture = textures[texnum];
    /* columns above the width mask are never addressed */
    width = texturewidthmask[texnum] + 1;
    if (width > texture->width)
        width = texture->width;
    height = texture->height;
    size = width * height + GPU_WALL_TEX2D_PAD;

    if (size > gpu_wall_tex2d_budget
        || R_Tex2DZoneTight(GPU_WALL_TEX2D_BUDGET - gpu_wall_tex2d_budget, size))
    {
        gpu_tex2d_dropped++;        /* over budget / zone tight -> CPU */
        return NULL;
    }

    block = Z_Malloc(size, PU_LEVEL, &gpu_wall_tex2d[texnum]);
    gpu_wall_tex2d_budget -= size;

    for (int x = 0; x < width; x++)
        memcpy(block + x * height, R_GetColumn(texnum, x), height);
    memset(block + width * height, 0, GPU_WALL_TEX2D_PAD);

    R_GPU_TextureDataUpdated(block, (unsigned int)size);

    return block;
}

/* Flat 2D block for the GPU param-masked midtexture path: the texture's
 * column_t posts decoded column-major (column x at block + x*height, each post's
 * texels placed at its topdelta, transparent gaps zero).  The GPU masked path
 * (R_GPU_MaskedBegin) draws only the post extents via {x,ytop,count} records, so
 * the zeros are never sampled and the result is byte-identical to the CPU
 * R_DrawMaskedColumn walk.
 *
 * Gated conservatively -- returns NULL (-> the CPU post walk draws it) unless the
 * decode is provably byte-exact: a power-of-two height <= 256 (the GPU masked
 * tier wraps vtex at the height, and R_DrawColumn samples posts with &127, which
 * agree only for a pow2 height in that range), every column lump-backed (a real
 * column_t to walk -- composited multipatch columns have no post structure), and
 * no post taller than 128 (beyond that the CPU's &127 post sampling wraps and a
 * linear block can't match).  Returning NULL here is also the kill switch: it
 * reverts masked surfaces to the unconditional CPU path. */
byte *R_GetMaskedTexture2D(int texnum)
{
    texture_t *texture;
    byte     **column_table;
    byte      *block;
    int        width;
    int        height;
    int        size;
    int        x;

    if (gpu_masked_tex2d == NULL || texnum < 0 || texnum >= numtextures)
        return NULL;
    if (gpu_masked_tex2d[texnum] != NULL)
        return gpu_masked_tex2d[texnum];

    texture = textures[texnum];
    height = texture->height;
    if (height <= 0 || height > 256 || (height & (height - 1)) != 0)
        return NULL;                    /* not a pow2 height in [1,256] -> CPU */

    width = texturewidthmask[texnum] + 1;
    if (width > texture->width)
        width = texture->width;
    size = width * height + GPU_WALL_TEX2D_PAD;

    if (size > gpu_masked_tex2d_budget
        || R_Tex2DZoneTight(GPU_MASKED_TEX2D_BUDGET - gpu_masked_tex2d_budget, size))
    {
        gpu_tex2d_dropped++;            /* over budget / zone tight -> CPU */
        return NULL;
    }

    column_table = R_GetColumnTable(texnum);
    if (column_table == NULL)
        return NULL;

    /* Validate every column is post-walkable (lump-backed, posts <= 128) before
     * allocating, so a decline never leaks a block. */
    for (x = 0; x < width; x++)
    {
        const column_t *column;

        if (texturecolumnlump[texnum][x] <= 0)
            return NULL;                /* composite column: no posts -> CPU */

        column = (const column_t *)(column_table[x] - 3);
        while (column->topdelta != 0xff)
        {
            if (column->length > 128)
                return NULL;            /* CPU samples post &127 -> can't match */
            column = (const column_t *)
                ((const byte *)column + column->length + 4);
        }
    }

    block = Z_Malloc(size, PU_LEVEL, &gpu_masked_tex2d[texnum]);
    gpu_masked_tex2d_budget -= size;
    memset(block, 0, size);

    for (x = 0; x < width; x++)
    {
        const column_t *column = (const column_t *)(column_table[x] - 3);

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

/* Flat 2D sprite block for the GPU affine-sprite path: patch posts
 * decoded column-major (column x at block + x*height, transparent
 * texels zero).  Records cover only post extents, so the zeros are
 * never sampled except at clamped boundary roundings. */
byte *R_GetSpriteTexture2D(int spritelump)
{
    patch_t *patch;
    byte *block;
    int width;
    int height;
    int size;

    if (gpu_sprite_tex2d == NULL)
        return NULL;
    if (spritelump < 0 || spritelump >= numspritelumps)
        return NULL;
    if (gpu_sprite_tex2d[spritelump] != NULL)
        return gpu_sprite_tex2d[spritelump];

    /* PU_LEVEL to match the drawn-sprite residency policy. */
    patch = W_CacheLumpNum(firstspritelump + spritelump, PU_LEVEL);
    width = SHORT(patch->width);
    height = SHORT(patch->height);
    if (width <= 0 || height <= 0 || height > 0xFFFF)
        return NULL;
    size = width * height;

    if (size > gpu_sprite_tex2d_budget
        || R_Tex2DZoneTight(GPU_SPRITE_TEX2D_BUDGET - gpu_sprite_tex2d_budget, size))
    {
        gpu_tex2d_dropped++;        /* over budget / zone tight -> CPU */
        return NULL;
    }

    block = Z_Malloc(size, PU_LEVEL, &gpu_sprite_tex2d[spritelump]);
    gpu_sprite_tex2d_budget -= size;
    memset(block, 0, size);

    for (int x = 0; x < width; x++)
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

static void GenerateTextureHashTable(void)
{
    texture_t **rover;
    int i;
    int key;

    textures_hashtable 
            = Z_Malloc(sizeof(texture_t *) * numtextures, PU_STATIC, 0);

    memset(textures_hashtable, 0, sizeof(texture_t *) * numtextures);

    // Add all textures to hash table

    for (i=0; i<numtextures; ++i)
    {
        // Store index

        textures[i]->index = i;

        // Vanilla Doom does a linear search of the texures array
        // and stops at the first entry it finds.  If there are two
        // entries with the same name, the first one in the array
        // wins. The new entry must therefore be added at the end
        // of the hash chain, so that earlier entries win.

        key = W_LumpNameHash(textures[i]->name) % numtextures;

        rover = &textures_hashtable[key];

        while (*rover != NULL)
        {
            rover = &(*rover)->next;
        }

        // Hook into hash table

        textures[i]->next = NULL;
        *rover = textures[i];
    }
}


//
// R_InitTextures
// Initializes the texture list
//  with the textures from the world map.
//
void R_InitTextures (void)
{
    maptexture_t*	mtexture;
    texture_t*		texture;
    mappatch_t*		mpatch;
    texpatch_t*		patch;

    int			i;
    int			j;

    byte*		maptex;
    byte*		maptex2;
    
    char		name[9];
    char*		names;
    char*		name_p;
    
    int*		patchlookup;
    
    int			nummappatches;
    int			offset;
    int			maxoff;
    int			maxoff2;
    int			numtextures1;
    int			numtextures2;

    byte*		directory;
    
    int			temp1;
    int			temp2;
    int			temp3;

    
    // Load the patch names from pnames.lmp.
    name[8] = 0;
    names = W_CacheLumpName (DEH_String("PNAMES"), PU_STATIC);
    nummappatches = ReadLE32Unaligned(names);
    name_p = names + 4;
    patchlookup = Z_Malloc(nummappatches*sizeof(*patchlookup), PU_STATIC, NULL);

    for (i = 0; i < nummappatches; i++)
    {
        M_StringCopy(name, name_p + i * 8, sizeof(name));
        patchlookup[i] = W_CheckNumForName(name);
    }
    W_ReleaseLumpName(DEH_String("PNAMES"));

    // Load the map texture definitions from textures.lmp.
    // The data is contained in one or two lumps,
    //  TEXTURE1 for shareware, plus TEXTURE2 for commercial.
    maptex = W_CacheLumpName (DEH_String("TEXTURE1"), PU_STATIC);
    numtextures1 = ReadLE32Unaligned(maptex);
    maxoff = W_LumpLength (W_GetNumForName (DEH_String("TEXTURE1")));
    directory = maptex + 4;
	
    if (W_CheckNumForName (DEH_String("TEXTURE2")) != -1)
    {
	maptex2 = W_CacheLumpName (DEH_String("TEXTURE2"), PU_STATIC);
	numtextures2 = ReadLE32Unaligned(maptex2);
	maxoff2 = W_LumpLength (W_GetNumForName (DEH_String("TEXTURE2")));
    }
    else
    {
	maptex2 = NULL;
	numtextures2 = 0;
	maxoff2 = 0;
    }
    numtextures = numtextures1 + numtextures2;
	
    textures = Z_Malloc (numtextures * sizeof(*textures), PU_STATIC, 0);
    texturecolumnlump = Z_Malloc (numtextures * sizeof(*texturecolumnlump), PU_STATIC, 0);
    texturecolumnofs = Z_Malloc (numtextures * sizeof(*texturecolumnofs), PU_STATIC, 0);
    texturecomposite = Z_Malloc (numtextures * sizeof(*texturecomposite), PU_STATIC, 0);
    texturecolumnptr = Z_Malloc (numtextures * sizeof(*texturecolumnptr), PU_STATIC, 0);
    texturecompositesize = Z_Malloc (numtextures * sizeof(*texturecompositesize), PU_STATIC, 0);
    texturewidthmask = Z_Malloc (numtextures * sizeof(*texturewidthmask), PU_STATIC, 0);
    textureheight = Z_Malloc (numtextures * sizeof(*textureheight), PU_STATIC, 0);
    memset (texturecolumnptr, 0, numtextures * sizeof(*texturecolumnptr));
    gpu_wall_tex2d = Z_Malloc (numtextures * sizeof(*gpu_wall_tex2d), PU_STATIC, 0);
    memset (gpu_wall_tex2d, 0, numtextures * sizeof(*gpu_wall_tex2d));
    gpu_wall_tex2d_budget = GPU_WALL_TEX2D_BUDGET;
    gpu_masked_tex2d = Z_Malloc (numtextures * sizeof(*gpu_masked_tex2d), PU_STATIC, 0);
    memset (gpu_masked_tex2d, 0, numtextures * sizeof(*gpu_masked_tex2d));
    gpu_masked_tex2d_budget = GPU_MASKED_TEX2D_BUDGET;

    //	Really complex printing shit...
    temp1 = W_GetNumForName (DEH_String("S_START"));  // P_???????
    temp2 = W_GetNumForName (DEH_String("S_END")) - 1;
    temp3 = ((temp2-temp1+63)/64) + ((numtextures+63)/64);

    // If stdout is a real console, use the classic vanilla "filling
    // up the box" effect, which uses backspace to "step back" inside
    // the box.  If stdout is a file, don't draw the box.

    if (I_ConsoleStdout())
    {
        printf("[");
        for (i = 0; i < temp3 + 9; i++)
            printf(" ");
        printf("]");
        for (i = 0; i < temp3 + 10; i++)
            printf("\b");
    }
	
    for (i=0 ; i<numtextures ; i++, directory += 4)
    {
	if (!(i&63))
	    printf (".");

	if (i == numtextures1)
	{
	    // Start looking in second texture file.
	    maptex = maptex2;
	    maxoff = maxoff2;
	    directory = maptex + 4;
	}
		
	offset = ReadLE32Unaligned(directory);

	if (offset > maxoff)
	    I_Error ("R_InitTextures: bad texture directory");
	
	mtexture = (maptexture_t *) ( (byte *)maptex + offset);

	texture = textures[i] =
	    Z_Malloc (sizeof(texture_t)
		      + sizeof(texpatch_t)*(SHORT(mtexture->patchcount)-1),
		      PU_STATIC, 0);
	
	texture->width = SHORT(mtexture->width);
	texture->height = SHORT(mtexture->height);
	texture->patchcount = SHORT(mtexture->patchcount);
	
	memcpy (texture->name, mtexture->name, sizeof(texture->name));
	mpatch = &mtexture->patches[0];
	patch = &texture->patches[0];

	for (j=0 ; j<texture->patchcount ; j++, mpatch++, patch++)
	{
	    patch->originx = SHORT(mpatch->originx);
	    patch->originy = SHORT(mpatch->originy);
	    patch->patch = patchlookup[SHORT(mpatch->patch)];
	    if (patch->patch == -1)
	    {
		I_Error ("R_InitTextures: Missing patch in texture %s",
			 texture->name);
	    }
	}		
	texturecolumnlump[i] = Z_Malloc (texture->width*sizeof(**texturecolumnlump), PU_STATIC,0);
	texturecolumnofs[i] = Z_Malloc (texture->width*sizeof(**texturecolumnofs), PU_STATIC,0);

	j = 1;
	while (j*2 <= texture->width)
	    j<<=1;

	texturewidthmask[i] = j-1;
	textureheight[i] = texture->height<<FRACBITS;
    }

    Z_Free(patchlookup);

    W_ReleaseLumpName(DEH_String("TEXTURE1"));
    if (maptex2)
        W_ReleaseLumpName(DEH_String("TEXTURE2"));
    
    // Precalculate whatever possible.	

    for (i=0 ; i<numtextures ; i++)
	R_GenerateLookup (i);
    
    // Create translation table for global animation.
    texturetranslation = Z_Malloc ((numtextures+1)*sizeof(*texturetranslation), PU_STATIC, 0);
    
    for (i=0 ; i<numtextures ; i++)
	texturetranslation[i] = i;

    GenerateTextureHashTable();
}



//
// R_InitFlats
//
void R_InitFlats (void)
{
    int		i;
	
    firstflat = W_GetNumForName (DEH_String("F_START")) + 1;
    lastflat = W_GetNumForName (DEH_String("F_END")) - 1;
    numflats = lastflat - firstflat + 1;
	
    // Create translation table for global animation.
    flattranslation = Z_Malloc ((numflats+1)*sizeof(*flattranslation), PU_STATIC, 0);
    flatlumpdata = Z_Malloc (numflats * sizeof(*flatlumpdata), PU_STATIC, 0);
    memset (flatlumpdata, 0, numflats * sizeof(*flatlumpdata));
    
    for (i=0 ; i<numflats ; i++)
	flattranslation[i] = i;
}


//
// R_InitSpriteLumps
// Finds the width and hoffset of all sprites in the wad,
//  so the sprite does not need to be cached completely
//  just for having the header info ready during rendering.
//
void R_InitSpriteLumps (void)
{
    int		i;
    patch_t	*patch;
	
    firstspritelump = W_GetNumForName (DEH_String("S_START")) + 1;
    lastspritelump = W_GetNumForName (DEH_String("S_END")) - 1;
    
    numspritelumps = lastspritelump - firstspritelump + 1;
    spritewidth = Z_Malloc (numspritelumps*sizeof(*spritewidth), PU_STATIC, 0);
    spriteoffset = Z_Malloc (numspritelumps*sizeof(*spriteoffset), PU_STATIC, 0);
    spritetopoffset = Z_Malloc (numspritelumps*sizeof(*spritetopoffset), PU_STATIC, 0);
    gpu_sprite_tex2d = Z_Malloc (numspritelumps*sizeof(*gpu_sprite_tex2d), PU_STATIC, 0);
    memset (gpu_sprite_tex2d, 0, numspritelumps*sizeof(*gpu_sprite_tex2d));
    gpu_sprite_tex2d_budget = GPU_SPRITE_TEX2D_BUDGET;
	
    for (i=0 ; i< numspritelumps ; i++)
    {
	if (!(i&63))
	    printf (".");

	patch = W_CacheLumpNum (firstspritelump+i, PU_CACHE);
	spritewidth[i] = SHORT(patch->width)<<FRACBITS;
	spriteoffset[i] = SHORT(patch->leftoffset)<<FRACBITS;
	spritetopoffset[i] = SHORT(patch->topoffset)<<FRACBITS;
    }
}



//
// R_InitColormaps
//
void R_InitColormaps (void)
{
    int	lump;

    // Load in the light tables, 
    //  256 byte align tables.
    lump = W_GetNumForName(DEH_String("COLORMAP"));
    colormaps = W_CacheLumpNum(lump, PU_STATIC);
}



//
// R_InitData
// Locates all the lumps
//  that will be used by all views
// Must be called after W_Init.
//
void R_InitData (void)
{
    R_InitTextures ();
    printf (".");
    R_InitFlats ();
    printf (".");
    R_InitSpriteLumps ();
    printf (".");
    R_InitColormaps ();
}



//
// R_FlatNumForName
// Retrieval, get a flat number for a flat name.
//
int R_FlatNumForName(const char *name)
{
    int		i;
    char	namet[9];

    i = W_CheckNumForName (name);

    if (i == -1)
    {
	namet[8] = 0;
	memcpy (namet, name,8);
	I_Error ("R_FlatNumForName: %s not found",namet);
    }
    return i - firstflat;
}

byte *R_GetFlatData(int flatnum, boolean permanent)
{
    byte *data;
    int lump;

    if (flatnum < 0 || flatnum >= numflats)
        I_Error("R_GetFlatData: bad flat %i", flatnum);

    if (flatlumpdata[flatnum] != NULL)
        return flatlumpdata[flatnum];

    lump = firstflat + flatnum;
    data = W_CacheLumpNum(lump, permanent ? PU_LEVEL : PU_STATIC);

    if (permanent)
        flatlumpdata[flatnum] = data;

    return data;
}




//
// R_CheckTextureNumForName
// Check whether texture is available.
// Filter out NoTexture indicator.
//
int R_CheckTextureNumForName(const char *name)
{
    texture_t *texture;
    int key;

    // "NoTexture" marker.
    if (name[0] == '-')		
	return 0;
		
    key = W_LumpNameHash(name) % numtextures;

    texture=textures_hashtable[key]; 
    
    while (texture != NULL)
    {
	if (!strncasecmp (texture->name, name, 8) )
	    return texture->index;

        texture = texture->next;
    }
    
    return -1;
}



//
// R_TextureNumForName
// Calls R_CheckTextureNumForName,
//  aborts with error message.
//
int R_TextureNumForName(const char *name)
{
    int		i;
	
    i = R_CheckTextureNumForName (name);

    if (i==-1)
    {
	I_Error ("R_TextureNumForName: %s not found",
		 name);
    }
    return i;
}




//
// R_CreateTextures
//  Register the level's texture set with the portable texture manager.
//
//  Runs at the end of R_PrecacheLevel, after every wall/sprite 2D block has been
//  built in zone and every present flat cached.  Each block is handed to the GPU
//  texture store (R_GPU_TexCreate*), which places it in the target's fast texture
//  memory or leaves it in SDRAM -- the engine never knows which.  The pixels stay
//  where the engine loaded them; the renderer later selects each texture by index
//  (R_GPU_Use*Texture).
//
//  For walls we also redirect texturecolumnptr[t][x] -> block + x*height so the
//  opaque column fallback and the sky column draw sample the same block as the
//  param path (their texel offsets then resolve against the texture's GPU base).
//  A texture used as a 2-sided midtexture is excluded: R_DrawMaskedColumn walks
//  its column_t posts on the CPU, so it keeps its SDRAM column table.
//
static void R_CreateTextures (void)
{
    char *masked;
    int   i;
    int   x;

    R_GPU_TexBeginLevel(numtextures, numflats, numspritelumps);
    R_GPU_TexSetColormap();     // colormap co-located first (fast offset 0)

    // A 2-sided midtexture keeps its SDRAM column table (CPU post walk).
    masked = Z_Malloc(numtextures, PU_STATIC, NULL);
    memset(masked, 0, numtextures);
    for (i = 0; i < numlines; i++)
    {
        if (lines[i].backsector == NULL)
            continue;                       // 1-sided: midtexture is opaque
        for (x = 0; x < 2; x++)
        {
            int sn = lines[i].sidenum[x];
            int mt;

            if (sn < 0)
                continue;
            mt = sides[sn].midtexture;
            if (mt > 0 && mt < numtextures)
                masked[mt] = 1;
        }
    }

    for (i = 0; i < numtextures; i++)
    {
        byte *blk = gpu_wall_tex2d[i];
        int   w;
        int   h;

        if (blk == NULL)
            continue;
        w = texturewidthmask[i] + 1;
        if (w > textures[i]->width)
            w = textures[i]->width;
        h = textures[i]->height;
        R_GPU_TexCreateWall(i, blk, w, h,
                            (unsigned int)(w * h) + GPU_WALL_TEX2D_PAD);
        if (!masked[i] && texturecolumnptr[i] != NULL)
            for (x = 0; x < w; x++)
                texturecolumnptr[i][x] = blk + x * h;
    }
    /* 2-sided midtextures additionally get a post-decoded block (separate from
     * the column-major wall block, which reads posts as garbage) so the GPU can
     * draw them; R_GetMaskedTexture2D declines (-> CPU) where it can't be exact. */
    for (i = 0; i < numtextures; i++)
    {
        byte *mblk;
        int   w;
        int   h;

        if (!masked[i])
            continue;
        mblk = R_GetMaskedTexture2D(i);
        if (mblk == NULL)
            continue;
        w = texturewidthmask[i] + 1;
        if (w > textures[i]->width)
            w = textures[i]->width;
        h = textures[i]->height;
        R_GPU_TexCreateMasked(i, mblk, w, h,
                              (unsigned int)(w * h) + GPU_WALL_TEX2D_PAD);
    }
    for (i = 0; i < numspritelumps; i++)
    {
        byte    *blk = gpu_sprite_tex2d[i];
        patch_t *p;
        int      w;
        int      h;

        if (blk == NULL)
            continue;
        p = W_CacheLumpNum(firstspritelump + i, PU_LEVEL);
        w = SHORT(p->width);
        h = SHORT(p->height);
        if (w <= 0 || h <= 0)
            continue;
        R_GPU_TexCreateSprite(i, blk, w, h, (unsigned int)(w * h));
    }
    for (i = 0; i < numflats; i++)
    {
        if (flatlumpdata[i] == NULL)
            continue;
        R_GPU_TexCreateFlat(i, flatlumpdata[i], 64, 64,
                            (unsigned int)lumpinfo[firstflat + i]->size);
    }

    Z_Free(masked);
    R_GPU_TexEndLevel();    // route the fast-texture domain now (GPU idle)

    if (gpu_tex2d_dropped > 0)
        printf("GPU textures: %d over budget -> CPU\n", gpu_tex2d_dropped);
}


//
// R_PrecacheLevel
// Preloads all relevant graphics for the level.
//
int		flatmemory;
int		texturememory;
int		spritememory;

void R_PrecacheLevel (void)
{
    char*		flatpresent;
    char*		texturepresent;
    char*		spritepresent;

    int			i;
    int			j;
    int			k;
    int			lump;
    
    texture_t*		texture;
    thinker_t*		th;
    spriteframe_t*	sf;

    if (flatlumpdata != NULL)
        memset(flatlumpdata, 0, numflats * sizeof(*flatlumpdata));

    /* PU_LEVEL frees auto-NULLed the per-texture owner pointers; the
     * 2D wall/sprite texture caches restart from a full budget each level. */
    gpu_wall_tex2d_budget = GPU_WALL_TEX2D_BUDGET;
    gpu_sprite_tex2d_budget = GPU_SPRITE_TEX2D_BUDGET;
    gpu_masked_tex2d_budget = GPU_MASKED_TEX2D_BUDGET;
    gpu_tex2d_dropped = 0;

    if (gpu_masked_tex2d != NULL)
        memset(gpu_masked_tex2d, 0, numtextures * sizeof(*gpu_masked_tex2d));

    if (texturecolumnptr != NULL)
        memset(texturecolumnptr, 0, numtextures * sizeof(*texturecolumnptr));

    /* Vanilla skips precache during demo playback, but the GPU renderer needs
     * the level's textures created (of_texture) and their 2D blocks prebuilt --
     * without it a demo draws entirely on the CPU, and the lazy first-sighting
     * builds drain the GPU mid-frame.  So precache for demos too. */

    // Precache flats.
    flatpresent = Z_Malloc(numflats, PU_STATIC, NULL);
    memset (flatpresent,0,numflats);	

    for (i=0 ; i<numsectors ; i++)
    {
	flatpresent[sectors[i].floorpic] = 1;
	flatpresent[sectors[i].ceilingpic] = 1;
    }

    P_ExpandAnimatedFlatPresence(flatpresent, numflats);
	
    flatmemory = 0;

    for (i=0 ; i<numflats ; i++)
    {
	if (flatpresent[i])
	{
	    byte *data;

	    lump = firstflat + i;
	    flatmemory += lumpinfo[lump]->size;
	    data = W_CacheLumpNum(lump, PU_LEVEL);
	    flatlumpdata[i] = data;
	    R_GPU_TextureDataUpdated(data, lumpinfo[lump]->size);
	}
    }

    Z_Free(flatpresent);
    
    // Precache textures.
    texturepresent = Z_Malloc(numtextures, PU_STATIC, NULL);
    memset (texturepresent,0, numtextures);
	
    for (i=0 ; i<numsides ; i++)
    {
	texturepresent[sides[i].toptexture] = 1;
	texturepresent[sides[i].midtexture] = 1;
	texturepresent[sides[i].bottomtexture] = 1;
    }

    // Sky texture is always present.
    // Note that F_SKY1 is the name used to
    //  indicate a sky floor/ceiling as a flat,
    //  while the sky texture is stored like
    //  a wall texture, with an episode dependend
    //  name.
    texturepresent[skytexture] = 1;
    // Reveal each switch's pressed state first (else flipping it drops the
    // screen-filling, up-close wall onto the slow CPU column path), THEN expand
    // animations -- so an animated switch face also gets all its frames.
    P_ExpandSwitchTexturePresence(texturepresent, numtextures);
    P_ExpandAnimatedTexturePresence(texturepresent, numtextures);
	
    texturememory = 0;
    for (i=0 ; i<numtextures ; i++)
    {
	if (!texturepresent[i])
	    continue;

	texture = textures[i];
	
	for (j=0 ; j<texture->patchcount ; j++)
	{
	    lump = texture->patches[j].patch;
	    texturememory += lumpinfo[lump]->size;
	    W_CacheLumpNum(lump, PU_LEVEL);
	}

	/* Prebuild the param-wall 2D block now: a lazy first-sighting
	 * build costs a full GPU pipeline drain mid-frame (openfpgaOS). */
	R_GetWallTexture2D(i);

	if (texturecompositesize[i] > 0)
	{
	    if (!texturecomposite[i])
		R_GenerateComposite(i);

	    if (texturecomposite[i])
	    {
		Z_ChangeTag(texturecomposite[i], PU_LEVEL);
		texturememory += texturecompositesize[i];
	    }

	    // R_GenerateComposite touches source patches as PU_CACHE.
	    for (j=0 ; j<texture->patchcount ; j++)
	    {
		lump = texture->patches[j].patch;
		W_CacheLumpNum(lump, PU_LEVEL);
	    }
	}

	R_BuildTextureColumnPointers(i);
    }

    Z_Free(texturepresent);
    
    // Precache sprites.
    spritepresent = Z_Malloc(numsprites, PU_STATIC, NULL);
    memset (spritepresent,0, numsprites);
	
    for (th = thinkercap.next ; th != &thinkercap ; th=th->next)
    {
	if (th->function.acp1 == (actionf_p1)P_MobjThinker)
	    spritepresent[((mobj_t *)th)->sprite] = 1;
    }
	
    spritememory = 0;
    for (i=0 ; i<numsprites ; i++)
    {
	if (!spritepresent[i])
	    continue;

	for (j=0 ; j<sprites[i].numframes ; j++)
	{
	    sf = &sprites[i].spriteframes[j];
	    for (k=0 ; k<8 ; k++)
	    {
		lump = firstspritelump + sf->lump[k];
		spritememory += lumpinfo[lump]->size;
		W_CacheLumpNum(lump , PU_LEVEL);
		/* Prebuild the affine-sprite block (openfpgaOS). */
		R_GetSpriteTexture2D(sf->lump[k]);
	    }
	}
    }

    Z_Free(spritepresent);
    R_GPU_TextureDataFlushAll();

    /* Register the level's textures with the portable texture store (fast
     * texture memory on Pocket, SDRAM on MiSTer -- decided per target). */
    R_CreateTextures();
}
