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
//	Here is a core component: drawing the floors and ceilings,
//	 while maintaining a per column clipping list only.
//	Moreover, the sky areas have to be determined.
//


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "i_system.h"
#include "z_zone.h"
#include "w_wad.h"

#include "doomdef.h"
#include "doomstat.h"
#include "of_fastram.h"

#include "r_local.h"
#include "p_spec.h"
#include "r_gpu.h"
#include "r_perf.h"
#include "r_sky.h"

#define R_CACHE_ALIGNED __attribute__((aligned(64)))


planefunction_t		floorfunc;
planefunction_t		ceilingfunc;

//
// opening
//

// Here comes the obnoxious "visplane".
#define MAXVISPLANES	1024
#define VISPLANE_HASH_SIZE 256
visplane_t		visplanes[MAXVISPLANES] R_CACHE_ALIGNED;
visplane_t*		lastvisplane;
visplane_t*		floorplane;
visplane_t*		ceilingplane;
static visplane_t*	visplane_hash[VISPLANE_HASH_SIZE] R_CACHE_ALIGNED;
static visplane_t*	visplane_next[MAXVISPLANES] R_CACHE_ALIGNED;

// ?
#define MAXOPENINGS	(SCREENWIDTH*256)
short			openings[MAXOPENINGS] R_CACHE_ALIGNED;
short*			lastopening;


//
// Clip values are the solid pixel bounding the range.
//  floorclip starts out SCREENHEIGHT
//  ceilingclip starts out -1
//
short			floorclip[SCREENWIDTH] R_CACHE_ALIGNED;
short			ceilingclip[SCREENWIDTH] R_CACHE_ALIGNED;

//
// spanstart holds the start of a plane span
// initialized to 0 at start
//
int			spanstart[SCREENHEIGHT] R_CACHE_ALIGNED;
int			spanstop[SCREENHEIGHT] R_CACHE_ALIGNED;

//
// texture mapping
//
lighttable_t**		planezlight;
byte*			planezlightrow;
fixed_t			planeheight;

fixed_t			yslope[SCREENHEIGHT] R_CACHE_ALIGNED;
fixed_t			distscale[SCREENWIDTH] R_CACHE_ALIGNED;
fixed_t			basexscale;
fixed_t			baseyscale;

fixed_t			cachedheight[SCREENHEIGHT] R_CACHE_ALIGNED;
fixed_t			cacheddistance[SCREENHEIGHT] R_CACHE_ALIGNED;
fixed_t			cachedxstep[SCREENHEIGHT] R_CACHE_ALIGNED;
fixed_t			cachedystep[SCREENHEIGHT] R_CACHE_ALIGNED;
unsigned		cachedlightindex[SCREENHEIGHT] R_CACHE_ALIGNED;

static unsigned int R_VisplaneHash(fixed_t height, int picnum, int lightlevel)
{
    unsigned int hash = (unsigned int)height;

    hash ^= hash >> 16;
    hash ^= (unsigned int)picnum * 131u;
    hash ^= (unsigned int)lightlevel * 17u;

    return hash & (VISPLANE_HASH_SIZE - 1);
}

static void R_LinkVisplane(visplane_t *plane)
{
    int index = plane - visplanes;
    unsigned int hash = R_VisplaneHash(plane->height,
                                       plane->picnum,
                                       plane->lightlevel);

    visplane_next[index] = visplane_hash[hash];
    visplane_hash[hash] = plane;
}

static boolean R_PlaneRangeIsOpen(const byte *top, int start, int stop)
{
    const byte *p = top + start;
    int count = stop - start + 1;

    while (count > 0 && (((uintptr_t)p) & 3u))
    {
	if (*p++ != 0xff)
	    return false;
	count--;
    }

    while (count >= 4)
    {
	if (*(const uint32_t *)p != 0xffffffffu)
	    return false;

	p += 4;
	count -= 4;
    }

    while (count-- > 0)
    {
	if (*p++ != 0xff)
	    return false;
    }

    return true;
}

//
// R_InitPlanes
// Only at game startup.
//
void R_InitPlanes (void)
{
  // Doh!
}


//
// R_MapPlane
//
// Uses global vars:
//  planeheight
//  ds_source
//  basexscale
//  baseyscale
//  viewx
//  viewy
//
// BASIC PRIMITIVE
//
OF_FASTTEXT void
R_MapPlane
( int		y,
  int		x1,
  int		x2 )
{
    angle_t	angle;
    fixed_t	distance;
    fixed_t	length;
    fixed_t	xfrac;
    fixed_t	yfrac;
    fixed_t	xstep;
    fixed_t	ystep;
    unsigned	index;
    int		gpu_light;
    lighttable_t* colormap = NULL;
    unsigned int perf_start;

    perf_start = R_PERF_DETAIL_BEGIN();
	
#ifdef RANGECHECK
    if (x2 < x1
     || x1 < 0
     || x2 >= viewwidth
     || y > viewheight)
    {
	I_Error ("R_MapPlane: %i, %i at %i",x1,x2,y);
    }
#endif

    if (planeheight != cachedheight[y])
    {
	cachedheight[y] = planeheight;
	distance = cacheddistance[y] = FixedMul (planeheight, yslope[y]);
	xstep = cachedxstep[y] = FixedMul (distance,basexscale);
	ystep = cachedystep[y] = FixedMul (distance,baseyscale);
	index = distance >> LIGHTZSHIFT;
	if (index >= MAXLIGHTZ )
	    index = MAXLIGHTZ-1;
	cachedlightindex[y] = index;
    }
    else
    {
	distance = cacheddistance[y];
	xstep = cachedxstep[y];
	ystep = cachedystep[y];
	index = cachedlightindex[y];
    }

    length = FixedMul (distance,distscale[x1]);
    angle = (viewangle + xtoviewangle[x1])>>ANGLETOFINESHIFT;
    xfrac = viewx + FixedMul(finecosine[angle], length);
    yfrac = -viewy - FixedMul(finesine[angle], length);

    if (fixedcolormap)
    {
	colormap = fixedcolormap;
	gpu_light = -1;
    }
    else
    {
	gpu_light = planezlightrow[index];
    }

    if (spanfunc == R_DrawSpan)
    {
	if (gpu_light >= 0)
	{
	    if (R_GPU_DrawSpanLightDirect(y, x1, x2, ds_source,
					  xfrac, yfrac, xstep, ystep,
					  gpu_light))
	    {
		goto done;
	    }
	}
	else if (R_GPU_DrawSpanDirect(y, x1, x2, ds_source,
				      xfrac, yfrac, xstep, ystep,
				      (const byte *)colormap))
	{
	    goto done;
	}
    }

    if (colormap == NULL)
	colormap = planezlight[index];

    ds_y = y;
    ds_x1 = x1;
    ds_x2 = x2;
    ds_xfrac = xfrac;
    ds_yfrac = yfrac;
    ds_xstep = xstep;
    ds_ystep = ystep;
    ds_colormap = colormap;

    // high or low detail
    spanfunc ();

  done:
    R_PERF_DETAIL_END(R_PERF_DETAIL_PLANE_MAP, perf_start);
}


//
// R_ClearPlanes
// At begining of frame.
//
void R_ClearPlanes (void)
{
    int		i;
    angle_t	angle;
    
    // opening / clipping determination
    for (i=0 ; i<viewwidth ; i++)
    {
	floorclip[i] = viewheight;
	ceilingclip[i] = -1;
    }

    lastvisplane = visplanes;
    lastopening = openings;
    memset(visplane_hash, 0, sizeof(visplane_hash));
    
    // texture calculation
    memset (cachedheight, 0xff, sizeof(cachedheight));

    // left to right mapping
    angle = (viewangle-ANG90)>>ANGLETOFINESHIFT;
	
    // scale will be unit scale at SCREENWIDTH/2 distance
    basexscale = FixedDiv (finecosine[angle],centerxfrac);
    baseyscale = -FixedDiv (finesine[angle],centerxfrac);
}




//
// R_FindPlane
//
visplane_t*
R_FindPlane
( fixed_t	height,
  int		picnum,
  int		lightlevel )
{
    visplane_t*	check;
    visplane_t* result;
    unsigned int hash;
    unsigned int perf_start;

    perf_start = R_PERF_DETAIL_BEGIN();
	
    if (picnum == skyflatnum)
    {
	height = 0;			// all skys map together
	lightlevel = 0;
    }
	
    hash = R_VisplaneHash(height, picnum, lightlevel);
    for (check = visplane_hash[hash]; check != NULL;
	 check = visplane_next[check - visplanes])
    {
	if (height == check->height
	    && picnum == check->picnum
	    && lightlevel == check->lightlevel)
	{
	    break;
	}
    }
    
			
    if (check != NULL)
    {
	result = check;
	goto done;
    }
		
    if (lastvisplane - visplanes == MAXVISPLANES)
	I_Error ("R_FindPlane: no more visplanes");
		
    check = lastvisplane++;

    check->height = height;
    check->picnum = picnum;
    check->lightlevel = lightlevel;
    check->minx = SCREENWIDTH;
    check->maxx = -1;
    
    memset (check->top,0xff,sizeof(check->top));
    R_LinkVisplane(check);
		
    result = check;

  done:
    R_PERF_DETAIL_END(R_PERF_DETAIL_BSP_FIND_PLANE, perf_start);
    return result;
}


//
// R_CheckPlane
//
OF_FASTTEXT visplane_t*
R_CheckPlane
( visplane_t*	pl,
  int		start,
  int		stop )
{
    int		intrl;
    int		intrh;
    int		unionl;
    int		unionh;
    unsigned int perf_start;

    perf_start = R_PERF_DETAIL_BEGIN();
	
    if (start < pl->minx)
    {
	intrl = pl->minx;
	unionl = start;
    }
    else
    {
	unionl = pl->minx;
	intrl = start;
    }
	
    if (stop > pl->maxx)
    {
	intrh = pl->maxx;
	unionh = stop;
    }
    else
    {
	unionh = pl->maxx;
	intrh = stop;
    }

    if (R_PlaneRangeIsOpen(pl->top, intrl, intrh))
    {
	pl->minx = unionl;
	pl->maxx = unionh;

	// use the same one
	R_PERF_DETAIL_END(R_PERF_DETAIL_BSP_CHECK_PLANE, perf_start);
	return pl;		
    }
	
    if (lastvisplane - visplanes == MAXVISPLANES)
	I_Error ("R_CheckPlane: no more visplanes");

    // make a new visplane
    lastvisplane->height = pl->height;
    lastvisplane->picnum = pl->picnum;
    lastvisplane->lightlevel = pl->lightlevel;

    pl = lastvisplane++;
    pl->minx = start;
    pl->maxx = stop;

    memset (pl->top,0xff,sizeof(pl->top));
    R_LinkVisplane(pl);
		
    R_PERF_DETAIL_END(R_PERF_DETAIL_BSP_CHECK_PLANE, perf_start);
    return pl;
}


//
// R_MakeSpans
//
static inline void __attribute__((always_inline))
R_MakeSpans
( int		x,
  int		t1,
  int		b1,
  int		t2,
  int		b2 )
{
    while (t1 < t2 && t1<=b1)
    {
	R_MapPlane (t1,spanstart[t1],x-1);
	t1++;
    }
    while (b1 > b2 && b1>=t1)
    {
	R_MapPlane (b1,spanstart[b1],x-1);
	b1--;
    }
	
    while (t2 < t1 && t2<=b2)
    {
	spanstart[t2] = x;
	t2++;
    }
    while (b2 > b1 && b2>=t2)
    {
	spanstart[b2] = x;
	b2--;
    }
}



//
// R_DrawPlanes
// At the end of each frame.
//
OF_FASTTEXT void R_DrawPlanes (void)
{
    visplane_t*		pl;
    int			light;
    int			x;
    int			stop;
    int			angle;
    int                 lumpnum;
    int                 flatnum;
    boolean             animated_flat;
				
#ifdef RANGECHECK
    if (ds_p - drawsegs > MAXDRAWSEGS)
	I_Error ("R_DrawPlanes: drawsegs overflow (%td)",
		 ds_p - drawsegs);
    
    if (lastvisplane - visplanes > MAXVISPLANES)
	I_Error ("R_DrawPlanes: visplane overflow (%td)",
		 lastvisplane - visplanes);
    
    if (lastopening - openings > MAXOPENINGS)
	I_Error ("R_DrawPlanes: opening overflow (%td)",
		 lastopening - openings);
#endif

    for (pl = visplanes ; pl < lastvisplane ; pl++)
    {
	if (pl->minx > pl->maxx)
	    continue;

	
	// sky flat
	if (pl->picnum == skyflatnum)
	{
	    dc_iscale = pspriteiscale>>detailshift;
	    
	    // Sky is allways drawn full bright,
	    //  i.e. colormaps[0] is used.
	    // Because of this hack, sky is not affected
	    //  by INVUL inverse mapping.
	    dc_colormap = colormaps;
	    dc_texturemid = skytexturemid;
	    for (x=pl->minx ; x <= pl->maxx ; x++)
	    {
		dc_yl = pl->top[x];
		dc_yh = pl->bottom[x];

		if (dc_yl <= dc_yh)
		{
		    angle = (viewangle + xtoviewangle[x])>>ANGLETOSKYSHIFT;
		    dc_x = x;
		    dc_source = R_GetColumn(skytexture, angle);
		    colfunc ();
		}
	    }
	    continue;
	}
	
	// regular flat
        flatnum = flattranslation[pl->picnum];
        animated_flat = P_IsAnimatedFlat(flatnum);
        lumpnum = firstflat + flatnum;
	ds_source = R_GetFlatData(flatnum, animated_flat);
	
	planeheight = abs(pl->height-viewz);
	light = (pl->lightlevel >> LIGHTSEGSHIFT)+extralight;

	if (light >= LIGHTLEVELS)
	    light = LIGHTLEVELS-1;

	if (light < 0)
	    light = 0;

	planezlight = zlight[light];
	planezlightrow = zlightrow[light];

	pl->top[pl->maxx+1] = 0xff;
	pl->top[pl->minx-1] = 0xff;
		
	stop = pl->maxx + 1;

	for (x=pl->minx ; x<= stop ; x++)
	{
	    R_MakeSpans(x,pl->top[x-1],
			pl->bottom[x-1],
			pl->top[x],
			pl->bottom[x]);
	}
	
        if (!animated_flat && !R_GPU_DeferLumpRelease(lumpnum))
            W_ReleaseLumpNum(lumpnum);
    }
}
