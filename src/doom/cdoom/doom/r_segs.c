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
//	All the clipping: columns, horizontal spans, sky columns.
//






#include <stdio.h>
#include <stdlib.h>

#include "i_system.h"
#include "of_fastram.h"

#include "doomdef.h"
#include "doomstat.h"

#include "r_local.h"
#include "r_gpu.h"
#include "r_perf.h"
#include "r_sky.h"


// OPTIMIZE: closed two sided lines as single sided

// True if any of the segs textures might be visible.
boolean		segtextured;	

// False if the back side is the same plane.
boolean		markfloor;	
boolean		markceiling;

boolean		maskedtexture;
int		toptexture;
int		bottomtexture;
int		midtexture;


angle_t		rw_normalangle;
// angle to line origin
int		rw_angle1;	

//
// regular wall
//
int		rw_x;
int		rw_stopx;
angle_t		rw_centerangle;
fixed_t		rw_offset;
fixed_t		rw_distance;
fixed_t		rw_scale;
fixed_t		rw_scalestep;
fixed_t		rw_midtexturemid;
fixed_t		rw_toptexturemid;
fixed_t		rw_bottomtexturemid;

int		worldtop;
int		worldbottom;
int		worldhigh;
int		worldlow;

fixed_t		pixhigh;
fixed_t		pixlow;
fixed_t		pixhighstep;
fixed_t		pixlowstep;

fixed_t		topfrac;
fixed_t		topstep;

fixed_t		bottomfrac;
fixed_t		bottomstep;


lighttable_t**	walllights;
byte*		walllightrows;

short*		maskedtexturecol;



//
// R_RenderMaskedSegRange
//
void
R_RenderMaskedSegRange
( drawseg_t*	ds,
  int		x1,
  int		x2 )
{
    unsigned	index;
    column_t*	col;
    int		lightnum;
    int		texnum;
    rendersegcache_t *cache;
    side_t      *side;
    boolean	masked_gpu;
    byte**	column_table;
    int		widthmask;
    
    // Calculate light table.
    // Use different light tables
    //   for horizontal / vertical / diagonal. Diagonal?
    // OPTIMIZE: get rid of LIGHTSEGSHIFT globally
    curline = ds->curline;
    cache = &rendersegcache[curline - segs];
    side = cache->sidedef;
    frontsector = cache->frontsector;
    backsector = cache->backsector;
    texnum = texturetranslation[side->midtexture];
    column_table = R_GetColumnTable(texnum);
    widthmask = R_GetTextureWidthMask(texnum);
	
    lightnum = (frontsector->lightlevel >> LIGHTSEGSHIFT)+extralight;
    lightnum += cache->lightbias;

    if (lightnum < 0)
    {
	walllights = scalelight[0];
	walllightrows = scalelightrow[0];
    }
    else if (lightnum >= LIGHTLEVELS)
    {
	walllights = scalelight[LIGHTLEVELS-1];
	walllightrows = scalelightrow[LIGHTLEVELS-1];
    }
    else
    {
	walllights = scalelight[lightnum];
	walllightrows = scalelightrow[lightnum];
    }

    maskedtexturecol = ds->maskedtexturecol;

    rw_scalestep = ds->scalestep;		
    spryscale = ds->scale1 + (x1 - ds->x1)*rw_scalestep;
    mfloorclip = ds->sprbottomclip;
    mceilingclip = ds->sprtopclip;
    
    // find positioning
    if (cache->pegflags & ML_DONTPEGBOTTOM)
    {
	dc_texturemid = frontsector->floorheight > backsector->floorheight
	    ? frontsector->floorheight : backsector->floorheight;
	dc_texturemid = dc_texturemid + textureheight[texnum] - viewz;
    }
    else
    {
	dc_texturemid =frontsector->ceilingheight<backsector->ceilingheight
	    ? frontsector->ceilingheight : backsector->ceilingheight;
	dc_texturemid = dc_texturemid - viewz;
    }
    dc_texturemid += side->rowoffset;
			
    if (fixedcolormap)
    {
	dc_colormap = fixedcolormap;
	maskedcolormaprow = R_GPU_ColormapRow((const byte *)dc_colormap);
    }
    else
    {
	maskedcolormaprow = -1;
    }

    // Param-masked path: the GPU evaluates the wall planes, posts
    // reduce to {x, ytop, count} records (openfpgaOS).
    masked_gpu = false;
    if (detailshift == 0)
    {
	masked_gpu = R_GPU_MaskedBegin(R_GetMaskedTexture2D(texnum),
				       textureheight[texnum] >> FRACBITS,
				       widthmask, dc_texturemid,
				       x1, x2, spryscale, rw_scalestep,
				       ds->gpu_mdistance, ds->gpu_moffset,
				       ds->gpu_mcenterangle);
    }

    // draw the columns
    for (dc_x = x1 ; dc_x <= x2 ; dc_x++)
    {
	// calculate lighting
	if (maskedtexturecol[dc_x] != SHRT_MAX)
	{
	    if (!fixedcolormap)
	    {
		index = spryscale>>LIGHTSCALESHIFT;

		if (index >=  MAXLIGHTSCALE )
		    index = MAXLIGHTSCALE-1;

		dc_colormap = walllights[index];
		maskedcolormaprow = walllightrows[index];
	    }
			
	    sprtopscreen = centeryfrac - FixedMul(dc_texturemid, spryscale);
	    dc_iscale = 0xffffffffu / (unsigned)spryscale;
	    
	    // draw the texture
	    col = (column_t *)
		(column_table[maskedtexturecol[dc_x] & widthmask] - 3);
			
	    R_DrawMaskedColumn (col);
	    maskedtexturecol[dc_x] = SHRT_MAX;
	}
	spryscale += rw_scalestep;
    }

    if (masked_gpu)
	R_GPU_MaskedEnd();

    maskedcolormaprow = -1;
	
}




//
// R_RenderSegLoop
// Draws zero, one, or two textures (and possibly a masked
//  texture) for walls.
// Can draw or mark the starting pixel of floor and ceiling
//  textures.
// CALLED: CORE LOOPING ROUTINE.
//
#define HEIGHTBITS		12
#define HEIGHTUNIT		(1<<HEIGHTBITS)
#define WALL_COLUMN_BATCH_LANES 16

static inline fixed_t __attribute__((always_inline))
R_TextureColumnForX(int x)
{
    angle_t angle;
    fixed_t texturecolumn;

    angle = (rw_centerangle + xtoviewangle[x])>>ANGLETOFINESHIFT;
    texturecolumn = rw_offset-FixedMul(finetangent[angle],rw_distance);

    return texturecolumn >> FRACBITS;
}

static inline void __attribute__((always_inline))
R_PrepareDrawColumn(fixed_t scale, int *iscale, int *lightrow,
		    lighttable_t **colormap)
{
    unsigned index;

    index = scale>>LIGHTSCALESHIFT;
    if (index >= MAXLIGHTSCALE)
	index = MAXLIGHTSCALE-1;

    dc_colormap = walllights[index];
    *colormap = dc_colormap;
    *lightrow = walllightrows[index];
    dc_iscale = 0xffffffffu / (unsigned)scale;
    *iscale = dc_iscale;
}

// Param-wall path setup: the GPU evaluates the wall's perspective planes,
// so each tier column reduces to a {x, ytop, count} record and the
// per-column texturecolumn/iscale math is skipped entirely.  Tiers whose
// texture has no flat 2D block keep the column batches.  Once per seg —
// deliberately NOT OF_FASTTEXT so it doesn't eat APP_BRAM budget.
static void R_GPUWallTiersBegin(int x, int stopx,
				fixed_t scale, fixed_t scalestep,
				int *tier_mid, int *tier_top,
				int *tier_bottom)
{
    *tier_mid = 0;
    *tier_top = 0;
    *tier_bottom = 0;

    if (detailshift != 0 || colfunc != basecolfunc)
	return;
    if (!midtexture && !toptexture && !bottomtexture)
	return;
    if (!R_GPU_WallSegBegin(x, stopx - 1, scale, scalestep,
			    rw_distance, rw_offset, rw_centerangle))
	return;

    if (midtexture)
	*tier_mid = R_GPU_WallTierBegin(0,
	    R_GetWallTexture2D(midtexture),
	    textureheight[midtexture] >> FRACBITS,
	    R_GetTextureWidthMask(midtexture), rw_midtexturemid);
    if (toptexture)
	*tier_top = R_GPU_WallTierBegin(0,
	    R_GetWallTexture2D(toptexture),
	    textureheight[toptexture] >> FRACBITS,
	    R_GetTextureWidthMask(toptexture), rw_toptexturemid);
    if (bottomtexture)
	*tier_bottom = R_GPU_WallTierBegin(1,
	    R_GetWallTexture2D(bottomtexture),
	    textureheight[bottomtexture] >> FRACBITS,
	    R_GetTextureWidthMask(bottomtexture), rw_bottomtexturemid);
}

static void R_DrawSegColumn(int x, byte **columns, int widthmask,
                            fixed_t texturecolumn,
                            fixed_t texturemid, int yl, int yh,
                            int lightrow)
{
    dc_x = x;
    dc_yl = yl;
    dc_yh = yh;
    dc_texturemid = texturemid;
    dc_source = columns[(int)texturecolumn & widthmask];

    if (detailshift == 0 && colfunc == basecolfunc
	&& R_GPU_DrawColumnLightDirect(x, yl, yh, dc_source,
				       texturemid, dc_iscale,
				       lightrow))
    {
	return;
    }

    colfunc ();
}

typedef struct
{
    int x;
    int lanes;
    int same_range;
    int yl[WALL_COLUMN_BATCH_LANES];
    int yh[WALL_COLUMN_BATCH_LANES];
    const byte *source[WALL_COLUMN_BATCH_LANES];
    int32_t t[WALL_COLUMN_BATCH_LANES];
    int32_t tstep[WALL_COLUMN_BATCH_LANES];
    lighttable_t *colormap[WALL_COLUMN_BATCH_LANES];
    uint8_t light[WALL_COLUMN_BATCH_LANES];
} wall_column_batch_t;

/* Not OF_FASTTEXT: mostly calls into regular-.text GPU emitters, and on
 * param-wall cores it only runs for fallback tiers — APP_BRAM is better
 * spent on the per-column loops. */
static void R_FlushWallColumnBatch(wall_column_batch_t *batch)
{
    if (batch->lanes <= 0)
	return;

    boolean drawn;

    if (batch->same_range)
    {
	drawn = R_GPU_DrawColumnLightBatchDirect(batch->x,
						 batch->yl[0],
						 batch->yh[0],
						 batch->lanes,
						 batch->source,
						 batch->t,
						 batch->tstep,
						 batch->light);
    }
    else
    {
	drawn = R_GPU_DrawColumnLightVarBatchDirect(batch->x, batch->lanes,
						    batch->yl, batch->yh,
						    batch->source, batch->t,
						    batch->tstep,
						    batch->light);
    }

    if (!drawn)
    {
	for (int i = 0; i < batch->lanes; i++)
	{
	    dc_x = batch->x + i;
	    dc_yl = batch->yl[i];
	    dc_yh = batch->yh[i];
	    dc_iscale = batch->tstep[i];
	    dc_texturemid = batch->t[i]
			  - (dc_yl - centery) * dc_iscale;
	    dc_source = (byte *)batch->source[i];
	    dc_colormap = batch->colormap[i];
	    colfunc ();
	}
    }

    batch->lanes = 0;
}

static inline void __attribute__((always_inline))
R_AddWallColumnBatch(wall_column_batch_t *batch,
		     int x, byte **columns, int widthmask,
		     fixed_t texturecolumn,
		     fixed_t texturemid, int yl, int yh,
		     int iscale, int lightrow,
		     lighttable_t *colormap)
{
    int lane;

    if (yh < yl)
	return;

    if (detailshift != 0 || colfunc != basecolfunc)
    {
	R_FlushWallColumnBatch(batch);
	R_DrawSegColumn(x, columns, widthmask, texturecolumn, texturemid,
			yl, yh, lightrow);
	return;
    }

    if (batch->lanes > 0
	&& (x != batch->x + batch->lanes
	    || batch->lanes == WALL_COLUMN_BATCH_LANES))
    {
	R_FlushWallColumnBatch(batch);
    }

    if (batch->lanes == 0)
    {
	batch->x = x;
	batch->same_range = 1;
    }
    else if (yl != batch->yl[0] || yh != batch->yh[0])
    {
	batch->same_range = 0;
    }

    lane = batch->lanes++;
    batch->yl[lane] = yl;
    batch->yh[lane] = yh;
    batch->source[lane] = columns[(int)texturecolumn & widthmask];
    batch->t[lane] = (int32_t)((uint32_t)texturemid
			       + (uint32_t)(yl - centery) * (uint32_t)iscale);
    batch->tstep[lane] = iscale;
    batch->colormap[lane] = colormap;
    batch->light[lane] = (uint8_t)lightrow;
}

OF_FASTTEXT void R_RenderSegLoop (void)
{
    int			yl;
    int			yh;
    int			mid;
    fixed_t		texturecolumn;
    int			top;
    int			bottom;
    int			x;
    int			stopx;
    fixed_t		scale;
    fixed_t		scalestep;
    fixed_t		topf;
    fixed_t		bottomf;
    fixed_t		pixh;
    fixed_t		pixl;
    fixed_t		pixhstep;
    fixed_t		pixlstep;
    fixed_t		top_step;
    fixed_t		bottom_step;
    short*		ceiling_clip;
    short*		floor_clip;
    visplane_t*		ceiling_plane;
    visplane_t*		floor_plane;
    short*		masked_col;
    boolean		do_markceiling;
    boolean		do_markfloor;
    boolean             texturecolumn_ready;
    boolean             drawcolumn_ready;
    int			lightrow;
    int			iscale;
    lighttable_t*	column_colormap;
    byte**		mid_columns;
    byte**		top_columns;
    byte**		bottom_columns;
    int			mid_widthmask;
    int			top_widthmask;
    int			bottom_widthmask;
    wall_column_batch_t upper_batch;
    wall_column_batch_t lower_batch;
    int			gpu_tier_mid;
    int			gpu_tier_top;
    int			gpu_tier_bottom;
    unsigned int        perf_start;

    perf_start = R_PERF_DETAIL_BEGIN();

    x = rw_x;
    stopx = rw_stopx;
    scale = rw_scale;
    scalestep = rw_scalestep;
    topf = topfrac;
    bottomf = bottomfrac;
    pixh = pixhigh;
    pixl = pixlow;
    pixhstep = pixhighstep;
    pixlstep = pixlowstep;
    top_step = topstep;
    bottom_step = bottomstep;
    ceiling_clip = ceilingclip;
    floor_clip = floorclip;
    ceiling_plane = ceilingplane;
    floor_plane = floorplane;
    masked_col = maskedtexturecol;
    do_markceiling = markceiling;
    do_markfloor = markfloor;
    upper_batch.lanes = 0;
    lower_batch.lanes = 0;
    mid_columns = NULL;
    top_columns = NULL;
    bottom_columns = NULL;
    mid_widthmask = 0;
    top_widthmask = 0;
    bottom_widthmask = 0;

    if (midtexture)
    {
	mid_columns = R_GetColumnTable(midtexture);
	mid_widthmask = R_GetTextureWidthMask(midtexture);
    }
    if (toptexture)
    {
	top_columns = R_GetColumnTable(toptexture);
	top_widthmask = R_GetTextureWidthMask(toptexture);
    }
    if (bottomtexture)
    {
	bottom_columns = R_GetColumnTable(bottomtexture);
	bottom_widthmask = R_GetTextureWidthMask(bottomtexture);
    }

    R_GPUWallTiersBegin(x, stopx, scale, scalestep,
			&gpu_tier_mid, &gpu_tier_top, &gpu_tier_bottom);

    for ( ; x < stopx ; x++)
    {
	texturecolumn_ready = false;
	drawcolumn_ready = false;

	// mark floor / ceiling areas
	yl = (topf+HEIGHTUNIT-1)>>HEIGHTBITS;

	// no space above wall?
	if (yl < ceiling_clip[x]+1)
	    yl = ceiling_clip[x]+1;
	
	if (do_markceiling)
	{
	    top = ceiling_clip[x]+1;
	    bottom = yl-1;

	    if (bottom >= floor_clip[x])
		bottom = floor_clip[x]-1;

	    if (top <= bottom)
	    {
		ceiling_plane->top[x] = top;
		ceiling_plane->bottom[x] = bottom;
	    }
	}
		
	yh = bottomf>>HEIGHTBITS;

	if (yh >= floor_clip[x])
	    yh = floor_clip[x]-1;

	if (do_markfloor)
	{
	    top = yh+1;
	    bottom = floor_clip[x]-1;
	    if (top <= ceiling_clip[x])
		top = ceiling_clip[x]+1;
	    if (top <= bottom)
	    {
		floor_plane->top[x] = top;
		floor_plane->bottom[x] = bottom;
	    }
	}
	
	// draw the wall tiers
	if (midtexture)
	{
	    // single sided line
	    if (yl <= yh)
	    {
		if (!gpu_tier_mid
		    || !R_GPU_WallTierColumn(0, x, yl, yh,
					     scale))
		{
		    texturecolumn = R_TextureColumnForX(x);
		    texturecolumn_ready = true;
		    R_PrepareDrawColumn(scale, &iscale, &lightrow,
					&column_colormap);
		    drawcolumn_ready = true;
		    R_AddWallColumnBatch(&upper_batch, x, mid_columns,
					 mid_widthmask, texturecolumn,
					 rw_midtexturemid, yl, yh, iscale,
					 lightrow, column_colormap);
		}
	    }
	    ceiling_clip[x] = viewheight;
	    floor_clip[x] = -1;
	}
	else
	{
	    // two sided line
	    if (toptexture)
	    {
		// top wall
		mid = pixh>>HEIGHTBITS;
		pixh += pixhstep;

		if (mid >= floor_clip[x])
		    mid = floor_clip[x]-1;

		if (mid >= yl)
		{
		    if (!gpu_tier_top
			|| !R_GPU_WallTierColumn(0, x, yl, mid,
						 scale))
		    {
			if (!texturecolumn_ready)
			{
			    texturecolumn = R_TextureColumnForX(x);
			    texturecolumn_ready = true;
			}
			if (!drawcolumn_ready)
			{
			    R_PrepareDrawColumn(scale, &iscale, &lightrow,
						&column_colormap);
			    drawcolumn_ready = true;
			}
			R_AddWallColumnBatch(&upper_batch, x, top_columns,
					     top_widthmask, texturecolumn,
					     rw_toptexturemid, yl, mid,
					     iscale, lightrow,
					     column_colormap);
		    }
		    ceiling_clip[x] = mid;
		}
		else
		    ceiling_clip[x] = yl-1;
	    }
	    else
	    {
		// no top wall
		if (do_markceiling)
		    ceiling_clip[x] = yl-1;
	    }
			
	    if (bottomtexture)
	    {
		// bottom wall
		mid = (pixl+HEIGHTUNIT-1)>>HEIGHTBITS;
		pixl += pixlstep;

		// no space above wall?
		if (mid <= ceiling_clip[x])
		    mid = ceiling_clip[x]+1;
		
		if (mid <= yh)
		{
		    if (!gpu_tier_bottom
			|| !R_GPU_WallTierColumn(1, x, mid, yh,
						 scale))
		    {
			if (!texturecolumn_ready)
			{
			    texturecolumn = R_TextureColumnForX(x);
			    texturecolumn_ready = true;
			}
			if (!drawcolumn_ready)
			{
			    R_PrepareDrawColumn(scale, &iscale, &lightrow,
						&column_colormap);
			    drawcolumn_ready = true;
			}
			R_AddWallColumnBatch(&lower_batch, x, bottom_columns,
					     bottom_widthmask, texturecolumn,
					     rw_bottomtexturemid, mid, yh,
					     iscale, lightrow,
					     column_colormap);
		    }
		    floor_clip[x] = mid;
		}
		else
		    floor_clip[x] = yh+1;
	    }
	    else
	    {
		// no bottom wall
		if (do_markfloor)
		    floor_clip[x] = yh+1;
	    }
			
	    if (maskedtexture)
	    {
		if (!texturecolumn_ready)
		{
		    texturecolumn = R_TextureColumnForX(x);
		    texturecolumn_ready = true;
		}
		// save texturecol
		//  for backdrawing of masked mid texture
		masked_col[x] = texturecolumn;
	    }
	}
		
	scale += scalestep;
	topf += top_step;
	bottomf += bottom_step;
    }

    R_GPU_WallTiersEnd();
    R_FlushWallColumnBatch(&upper_batch);
    R_FlushWallColumnBatch(&lower_batch);

    rw_x = x;
    rw_scale = scale;
    topfrac = topf;
    bottomfrac = bottomf;
    pixhigh = pixh;
    pixlow = pixl;

    R_PERF_DETAIL_END(R_PERF_DETAIL_BSP_SEG_LOOP, perf_start);
}

/* Not OF_FASTTEXT: APP_BRAM is full; the planes-only loop is lighter
 * than the textured loop and the 32 KB I$ holds it fine. */
static void R_RenderPlaneSegLoop(void)
{
    int			yl;
    int			yh;
    int			top;
    int			bottom;
    int			x;
    int			stopx;
    fixed_t		topf;
    fixed_t		bottomf;
    fixed_t		top_step;
    fixed_t		bottom_step;
    short*		ceiling_clip;
    short*		floor_clip;
    visplane_t*		ceiling_plane;
    visplane_t*		floor_plane;
    boolean		do_markceiling;
    boolean		do_markfloor;
    unsigned int        perf_start;

    perf_start = R_PERF_DETAIL_BEGIN();

    x = rw_x;
    stopx = rw_stopx;
    topf = topfrac;
    bottomf = bottomfrac;
    top_step = topstep;
    bottom_step = bottomstep;
    ceiling_clip = ceilingclip;
    floor_clip = floorclip;
    ceiling_plane = ceilingplane;
    floor_plane = floorplane;
    do_markceiling = markceiling;
    do_markfloor = markfloor;

    for ( ; x < stopx ; x++)
    {
	yl = (topf+HEIGHTUNIT-1)>>HEIGHTBITS;
	if (yl < ceiling_clip[x]+1)
	    yl = ceiling_clip[x]+1;

	if (do_markceiling)
	{
	    top = ceiling_clip[x]+1;
	    bottom = yl-1;

	    if (bottom >= floor_clip[x])
		bottom = floor_clip[x]-1;

	    if (top <= bottom)
	    {
		ceiling_plane->top[x] = top;
		ceiling_plane->bottom[x] = bottom;
	    }
	}

	yh = bottomf>>HEIGHTBITS;
	if (yh >= floor_clip[x])
	    yh = floor_clip[x]-1;

	if (do_markfloor)
	{
	    top = yh+1;
	    bottom = floor_clip[x]-1;
	    if (top <= ceiling_clip[x])
		top = ceiling_clip[x]+1;
	    if (top <= bottom)
	    {
		floor_plane->top[x] = top;
		floor_plane->bottom[x] = bottom;
	    }
	}

	if (do_markceiling)
	    ceiling_clip[x] = yl-1;
	if (do_markfloor)
	    floor_clip[x] = yh+1;

	topf += top_step;
	bottomf += bottom_step;
    }

    rw_x = x;
    topfrac = topf;
    bottomfrac = bottomf;

    R_PERF_DETAIL_END(R_PERF_DETAIL_BSP_SEG_LOOP, perf_start);
}




//
// R_StoreWallRange
// A wall segment will be drawn
//  between start and stop pixels (inclusive).
//
OF_FASTTEXT void
R_StoreWallRange
( int	start,
  int	stop )
{
    fixed_t		hyp;
    fixed_t		sineval;
    angle_t		distangle, offsetangle;
    fixed_t		vtop;
    int			lightnum;
    fixed_t             side_textureoffset;
    fixed_t             side_rowoffset;
    short               side_toptexture;
    short               side_bottomtexture;
    short               side_midtexture;
    short               line_pegflags;
    signed char         line_lightbias;
    rendersegcache_t   *cache;
    vertex_t           *seg_v1;
    angle_t             seg_normalangle;
    fixed_t             seg_offset;
    unsigned int        perf_start;

    // don't overflow and crash
    if (ds_p == &drawsegs[MAXDRAWSEGS])
	return;		

    if (start > stop)
	return;

    perf_start = R_PERF_DETAIL_BEGIN();
		
#ifdef RANGECHECK
    if (start >=viewwidth || start > stop)
	I_Error ("Bad R_RenderWallRange: %i to %i", start , stop);
#endif
    
    cache = cursegcache;

    sidedef = cache->sidedef;
    linedef = cache->linedef;
    side_textureoffset = sidedef->textureoffset;
    side_rowoffset = sidedef->rowoffset;
    side_toptexture = sidedef->toptexture;
    side_bottomtexture = sidedef->bottomtexture;
    side_midtexture = sidedef->midtexture;
    line_pegflags = cache->pegflags;
    line_lightbias = cache->lightbias;
    seg_v1 = cache->v1;
    seg_normalangle = cache->normalangle;
    seg_offset = cache->offset;

    // mark the segment as visible for auto map
    linedef->flags |= ML_MAPPED;
    
    rw_x = start;
    rw_stopx = stop+1;
    
    // calculate texture boundaries
    //  and decide if floor / ceiling marks are needed
    worldtop = frontsector->ceilingheight - viewz;
    worldbottom = frontsector->floorheight - viewz;
	
    midtexture = toptexture = bottomtexture = maskedtexture = 0;
    ds_p->maskedtexturecol = NULL;
	
    if (!backsector)
    {
	// single sided line
	midtexture = texturetranslation[side_midtexture];
	// a single sided line is terminal, so it must mark ends
	markfloor = markceiling = true;
	if (line_pegflags & ML_DONTPEGBOTTOM)
	{
	    vtop = frontsector->floorheight +
		textureheight[side_midtexture];
	    // bottom of texture at bottom
	    rw_midtexturemid = vtop - viewz;	
	}
	else
	{
	    // top of texture at top
	    rw_midtexturemid = worldtop;
	}
	rw_midtexturemid += side_rowoffset;

	ds_p->silhouette = SIL_BOTH;
	ds_p->sprtopclip = screenheightarray;
	ds_p->sprbottomclip = negonearray;
	ds_p->bsilheight = INT_MAX;
	ds_p->tsilheight = INT_MIN;
    }
    else
    {
	// two sided line
	ds_p->sprtopclip = ds_p->sprbottomclip = NULL;
	ds_p->silhouette = 0;
	
	if (frontsector->floorheight > backsector->floorheight)
	{
	    ds_p->silhouette = SIL_BOTTOM;
	    ds_p->bsilheight = frontsector->floorheight;
	}
	else if (backsector->floorheight > viewz)
	{
	    ds_p->silhouette = SIL_BOTTOM;
	    ds_p->bsilheight = INT_MAX;
	    // ds_p->sprbottomclip = negonearray;
	}
	
	if (frontsector->ceilingheight < backsector->ceilingheight)
	{
	    ds_p->silhouette |= SIL_TOP;
	    ds_p->tsilheight = frontsector->ceilingheight;
	}
	else if (backsector->ceilingheight < viewz)
	{
	    ds_p->silhouette |= SIL_TOP;
	    ds_p->tsilheight = INT_MIN;
	    // ds_p->sprtopclip = screenheightarray;
	}
		
	if (backsector->ceilingheight <= frontsector->floorheight)
	{
	    ds_p->sprbottomclip = negonearray;
	    ds_p->bsilheight = INT_MAX;
	    ds_p->silhouette |= SIL_BOTTOM;
	}
	
	if (backsector->floorheight >= frontsector->ceilingheight)
	{
	    ds_p->sprtopclip = screenheightarray;
	    ds_p->tsilheight = INT_MIN;
	    ds_p->silhouette |= SIL_TOP;
	}
	
	worldhigh = backsector->ceilingheight - viewz;
	worldlow = backsector->floorheight - viewz;
		
	// hack to allow height changes in outdoor areas
	if (frontsector->ceilingpic == skyflatnum 
	    && backsector->ceilingpic == skyflatnum)
	{
	    worldtop = worldhigh;
	}
	
			
	if (worldlow != worldbottom 
	    || backsector->floorpic != frontsector->floorpic
	    || backsector->lightlevel != frontsector->lightlevel)
	{
	    markfloor = true;
	}
	else
	{
	    // same plane on both sides
	    markfloor = false;
	}
	
			
	if (worldhigh != worldtop 
	    || backsector->ceilingpic != frontsector->ceilingpic
	    || backsector->lightlevel != frontsector->lightlevel)
	{
	    markceiling = true;
	}
	else
	{
	    // same plane on both sides
	    markceiling = false;
	}
	
	if (backsector->ceilingheight <= frontsector->floorheight
	    || backsector->floorheight >= frontsector->ceilingheight)
	{
	    // closed door
	    markceiling = markfloor = true;
	}
	

	if (worldhigh < worldtop)
	{
	    // top texture
	    toptexture = texturetranslation[side_toptexture];
	    if (line_pegflags & ML_DONTPEGTOP)
	    {
		// top of texture at top
		rw_toptexturemid = worldtop;
	    }
	    else
	    {
		vtop =
		    backsector->ceilingheight
		    + textureheight[side_toptexture];
		
		// bottom of texture
		rw_toptexturemid = vtop - viewz;	
	    }
	}
	if (worldlow > worldbottom)
	{
	    // bottom texture
	    bottomtexture = texturetranslation[side_bottomtexture];

	    if (line_pegflags & ML_DONTPEGBOTTOM)
	    {
		// bottom of texture at bottom
		// top of texture at top
		rw_bottomtexturemid = worldtop;
	    }
	    else	// top of texture at top
		rw_bottomtexturemid = worldlow;
	}
	rw_toptexturemid += side_rowoffset;
	rw_bottomtexturemid += side_rowoffset;
	
	// allocate space for masked texture tables
	if (side_midtexture)
	{
	    // masked midtexture
	    maskedtexture = true;
	    ds_p->maskedtexturecol = maskedtexturecol = lastopening - rw_x;
	    lastopening += rw_stopx - rw_x;
	}
    }
    
    segtextured = midtexture | toptexture | bottomtexture | maskedtexture;

    if (segtextured)
    {
	// calculate light table
	//  use different light tables
	//  for horizontal / vertical / diagonal
	// OPTIMIZE: get rid of LIGHTSEGSHIFT globally
	if (!fixedcolormap)
	{
	    lightnum = (frontsector->lightlevel >> LIGHTSEGSHIFT)+extralight;
	    lightnum += line_lightbias;

	    if (lightnum < 0)		
	    {
		walllights = scalelight[0];
		walllightrows = scalelightrow[0];
	    }
	    else if (lightnum >= LIGHTLEVELS)
	    {
		walllights = scalelight[LIGHTLEVELS-1];
		walllightrows = scalelightrow[LIGHTLEVELS-1];
	    }
	    else
	    {
		walllights = scalelight[lightnum];
		walllightrows = scalelightrow[lightnum];
	    }
	}
    }
    
    // if a floor / ceiling plane is on the wrong side
    //  of the view plane, it is definitely invisible
    //  and doesn't need to be marked.
    
  
    if (frontsector->floorheight >= viewz)
    {
	// above view plane
	markfloor = false;
    }
    
    if (frontsector->ceilingheight <= viewz 
	&& frontsector->ceilingpic != skyflatnum)
    {
	// below view plane
	markceiling = false;
    }

    if (!segtextured
	&& !markceiling
	&& !markfloor
	&& ds_p->silhouette == 0)
    {
	R_PERF_DETAIL_COUNT(R_PERF_DETAIL_WALL_SKIPPED);
	R_PERF_DETAIL_END(R_PERF_DETAIL_BSP_STORE_WALL, perf_start);
	return;
    }

    if (segtextured)
	R_PERF_DETAIL_COUNT(R_PERF_DETAIL_WALL_TEXTURED);
    else if (ds_p->silhouette != 0)
	R_PERF_DETAIL_COUNT(R_PERF_DETAIL_WALL_SILHOUETTE);
    else
	R_PERF_DETAIL_COUNT(R_PERF_DETAIL_WALL_PLANE);

    // calculate rw_distance for scale calculation
    rw_normalangle = seg_normalangle;
    offsetangle = abs((int)rw_normalangle-(int)rw_angle1);

    if (offsetangle > ANG90)
	offsetangle = ANG90;

    distangle = ANG90 - offsetangle;
    hyp = R_VertexViewDist(seg_v1);
    sineval = finesine[distangle>>ANGLETOFINESHIFT];
    rw_distance = FixedMul (hyp, sineval);

    ds_p->x1 = start;
    ds_p->x2 = stop;
    ds_p->curline = curline;

    // calculate scale at both ends and step
    ds_p->scale1 = rw_scale =
	R_ScaleFromGlobalAngle (viewangle + xtoviewangle[start]);

    if (stop > start )
    {
	ds_p->scale2 = R_ScaleFromGlobalAngle (viewangle + xtoviewangle[stop]);
	ds_p->scalestep = rw_scalestep =
	    (ds_p->scale2 - rw_scale) / (stop-start);
    }
    else
    {
	// UNUSED: try to fix the stretched line bug
#if 0
	if (rw_distance < FRACUNIT/2)
	{
	    fixed_t		trx,try;
	    fixed_t		gxt,gyt;

	    trx = seg_v1->x - viewx;
	    try = seg_v1->y - viewy;

	    gxt = FixedMul(trx,viewcos);
	    gyt = -FixedMul(try,viewsin);
	    ds_p->scale1 = FixedDiv(projection, gxt-gyt)<<detailshift;
	}
#endif
	ds_p->scale2 = ds_p->scale1;
    }

    if (segtextured)
    {
	offsetangle = rw_normalangle-rw_angle1;

	if (offsetangle > ANG180)
	    offsetangle = -offsetangle;

	if (offsetangle > ANG90)
	    offsetangle = ANG90;

	sineval = finesine[offsetangle >>ANGLETOFINESHIFT];
	rw_offset = FixedMul (hyp, sineval);

	if (rw_normalangle-rw_angle1 < ANG180)
	    rw_offset = -rw_offset;

	rw_offset += side_textureoffset + seg_offset;
	rw_centerangle = ANG90 + viewangle - rw_normalangle;

	// Stash for the param-masked path (openfpgaOS).
	if (maskedtexture)
	{
	    ds_p->gpu_moffset = rw_offset;
	    ds_p->gpu_mdistance = rw_distance;
	    ds_p->gpu_mcenterangle = rw_centerangle;
	}
    }

    
    // calculate incremental stepping values for texture edges
    worldtop >>= 4;
    worldbottom >>= 4;
	
    topstep = -FixedMul (rw_scalestep, worldtop);
    topfrac = (centeryfrac>>4) - FixedMul (worldtop, rw_scale);

    bottomstep = -FixedMul (rw_scalestep,worldbottom);
    bottomfrac = (centeryfrac>>4) - FixedMul (worldbottom, rw_scale);
	
    if (backsector)
    {	
	worldhigh >>= 4;
	worldlow >>= 4;

	if (worldhigh < worldtop)
	{
	    pixhigh = (centeryfrac>>4) - FixedMul (worldhigh, rw_scale);
	    pixhighstep = -FixedMul (rw_scalestep,worldhigh);
	}
	
	if (worldlow > worldbottom)
	{
	    pixlow = (centeryfrac>>4) - FixedMul (worldlow, rw_scale);
	    pixlowstep = -FixedMul (rw_scalestep,worldlow);
	}
    }
    
    // render it
    if (markceiling)
    {
	visplane_t *new_ceilingplane =
	    R_CheckPlane (ceilingplane, rw_x, rw_stopx-1);

	if (new_ceilingplane != ceilingplane)
	{
	    ceilingplane = new_ceilingplane;
	    R_UpdateSectorPlaneCache(frontsector, NULL, ceilingplane,
				     false, true);
	}
	else
	{
	    ceilingplane = new_ceilingplane;
	}
    }
    
    if (markfloor)
    {
	visplane_t *new_floorplane =
	    R_CheckPlane (floorplane, rw_x, rw_stopx-1);

	if (new_floorplane != floorplane)
	{
	    floorplane = new_floorplane;
	    R_UpdateSectorPlaneCache(frontsector, floorplane, NULL,
				     true, false);
	}
	else
	{
	    floorplane = new_floorplane;
	}
    }

    if (segtextured)
	R_RenderSegLoop ();
    else
	R_RenderPlaneSegLoop ();

    
    // save sprite clipping info
    if ( ((ds_p->silhouette & SIL_TOP) || maskedtexture)
	 && !ds_p->sprtopclip)
    {
	memcpy (lastopening, ceilingclip+start, sizeof(*lastopening)*(rw_stopx-start));
	ds_p->sprtopclip = lastopening - start;
	lastopening += rw_stopx - start;
    }
    
    if ( ((ds_p->silhouette & SIL_BOTTOM) || maskedtexture)
	 && !ds_p->sprbottomclip)
    {
	memcpy (lastopening, floorclip+start, sizeof(*lastopening)*(rw_stopx-start));
	ds_p->sprbottomclip = lastopening - start;
	lastopening += rw_stopx - start;	
    }

    if (maskedtexture && !(ds_p->silhouette&SIL_TOP))
    {
	ds_p->silhouette |= SIL_TOP;
	ds_p->tsilheight = INT_MIN;
    }
    if (maskedtexture && !(ds_p->silhouette&SIL_BOTTOM))
    {
	ds_p->silhouette |= SIL_BOTTOM;
	ds_p->bsilheight = INT_MAX;
    }

    if (!segtextured && ds_p->silhouette == 0)
    {
	R_PERF_DETAIL_END(R_PERF_DETAIL_BSP_STORE_WALL, perf_start);
	return;
    }

    ds_p++;
    R_PERF_DETAIL_END(R_PERF_DETAIL_BSP_STORE_WALL, perf_start);
}
