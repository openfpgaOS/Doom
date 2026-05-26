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
//	BSP traversal, handling of LineSegs for rendering.
//




#include "doomdef.h"

#include <stdint.h>

#include "m_bbox.h"

#include "i_system.h"
#include "of_fastram.h"

#include "r_main.h"
#include "r_perf.h"
#include "r_plane.h"
#include "r_things.h"

// State.
#include "doomstat.h"
#include "r_state.h"
#include "r_bsp.h"
#include "z_zone.h"

#include "tables.h"

//#include "r_local.h"

#define R_CACHE_ALIGNED __attribute__((aligned(64)))



seg_t*		curline;
side_t*		sidedef;
line_t*		linedef;
sector_t*	frontsector;
sector_t*	backsector;
rendersegcache_t* rendersegcache;
rendersegcache_t* cursegcache;

drawseg_t	drawsegs[MAXDRAWSEGS] R_CACHE_ALIGNED;
drawseg_t*	ds_p;

#define BBOX_ANGLE_CACHE_SIZE 4096

typedef struct
{
    fixed_t x;
    fixed_t y;
    angle_t angle;
    int validcount;
} bbox_angle_cache_t;

static bbox_angle_cache_t bbox_angle_cache[BBOX_ANGLE_CACHE_SIZE]
    R_CACHE_ALIGNED;

typedef struct
{
    fixed_t x;
    fixed_t y;
    fixed_t dx;
    fixed_t dy;
    fixed_t bbox[2][4];
    unsigned short children[2];
    unsigned short pad[6];
} bsp_render_node_t;

static bsp_render_node_t *bsp_render_nodes;

typedef struct
{
    int validcount;
    visplane_t *floorplane;
    visplane_t *ceilingplane;
} sector_plane_cache_t;

static sector_plane_cache_t *sector_plane_cache;

static void R_FillSegRenderData(void)
{
    for (int i = 0; i < numsegs; i++)
    {
	seg_t *seg = &segs[i];
	rendersegcache_t *dst = &rendersegcache[i];
	side_t *side = seg->sidedef;
	line_t *line = seg->linedef;

	dst->v1 = seg->v1;
	dst->v2 = seg->v2;
	dst->frontsector = seg->frontsector;
	dst->backsector = seg->backsector;
	dst->sidedef = side;
	dst->linedef = line;
	dst->angle = seg->angle;
	dst->normalangle = seg->angle + ANG90;
	dst->offset = seg->offset;
	dst->pegflags = line->flags & (ML_DONTPEGTOP | ML_DONTPEGBOTTOM);

	if (seg->v1->y == seg->v2->y)
	    dst->lightbias = -1;
	else if (seg->v1->x == seg->v2->x)
	    dst->lightbias = 1;
	else
	    dst->lightbias = 0;
    }
}

void R_BuildSegRenderData(void)
{
    byte *raw;

    if (numsegs <= 0 || segs == NULL)
    {
	rendersegcache = NULL;
	return;
    }

    raw = Z_Malloc(numsegs * sizeof(*rendersegcache) + 63,
		   PU_LEVEL, 0);
    rendersegcache = (rendersegcache_t *)
	(((uintptr_t)raw + 63u) & ~(uintptr_t)63u);

    if (numsectors > 0 && sectors != NULL)
    {
	raw = Z_Malloc(numsectors * sizeof(*sector_plane_cache) + 63,
		       PU_LEVEL, 0);
	sector_plane_cache = (sector_plane_cache_t *)
	    (((uintptr_t)raw + 63u) & ~(uintptr_t)63u);
	memset(sector_plane_cache, 0,
	       numsectors * sizeof(*sector_plane_cache));
    }
    else
    {
	sector_plane_cache = NULL;
    }

    R_FillSegRenderData();
}

void R_UpdateSegRenderData(void)
{
    if (rendersegcache == NULL)
	R_BuildSegRenderData();
    else
	R_FillSegRenderData();
}

void R_UpdateSectorPlaneCache(sector_t *sector,
			      visplane_t *floor,
			      visplane_t *ceiling,
			      boolean update_floor,
			      boolean update_ceiling)
{
    sector_plane_cache_t *plane_cache;
    int sector_index;

    if (sector_plane_cache == NULL || sector == NULL)
	return;

    sector_index = sector - sectors;
    if ((unsigned)sector_index >= (unsigned)numsectors)
	return;

    plane_cache = &sector_plane_cache[sector_index];
    if (plane_cache->validcount != validcount)
	return;

    if (update_floor)
	plane_cache->floorplane = floor;

    if (update_ceiling)
	plane_cache->ceilingplane = ceiling;
}

void R_BuildBSPRenderData(void)
{
    byte *raw;

    if (numnodes <= 0 || nodes == NULL)
    {
	bsp_render_nodes = NULL;
	return;
    }

    raw = Z_Malloc(numnodes * sizeof(*bsp_render_nodes) + 63,
		   PU_LEVEL, 0);
    bsp_render_nodes = (bsp_render_node_t *)
	(((uintptr_t)raw + 63u) & ~(uintptr_t)63u);

    for (int i = 0; i < numnodes; i++)
    {
	node_t *src = &nodes[i];
	bsp_render_node_t *dst = &bsp_render_nodes[i];

	dst->x = src->x;
	dst->y = src->y;
	dst->dx = src->dx;
	dst->dy = src->dy;
	dst->children[0] = src->children[0];
	dst->children[1] = src->children[1];

	for (int side = 0; side < 2; side++)
	{
	    for (int k = 0; k < 4; k++)
		dst->bbox[side][k] = src->bbox[side][k];
	}
    }
}

static inline fixed_t R_BspAbs(fixed_t value)
{
    return value < 0 ? -value : value;
}

static inline int R_BspSlopeDiv(unsigned int num, unsigned int den)
{
    unsigned int ans;

    if (den < 512)
	return SLOPERANGE;

    ans = (num << 3) / (den >> 8);
    return ans <= SLOPERANGE ? (int)ans : SLOPERANGE;
}

static inline int __attribute__((always_inline))
R_PointOnSideBSP(const bsp_render_node_t *node)
{
    fixed_t dx;
    fixed_t dy;
    fixed_t left;
    fixed_t right;

    if (!node->dx)
    {
	if (viewx <= node->x)
	    return node->dy > 0;

	return node->dy < 0;
    }

    if (!node->dy)
    {
	if (viewy <= node->y)
	    return node->dx < 0;

	return node->dx > 0;
    }

    dx = viewx - node->x;
    dy = viewy - node->y;

    if ((node->dy ^ node->dx ^ dx ^ dy) & 0x80000000)
    {
	if ((node->dy ^ dx) & 0x80000000)
	    return 1;
	return 0;
    }

    left = FixedMul(node->dy >> FRACBITS, dx);
    right = FixedMul(dy, node->dx >> FRACBITS);

    return right >= left;
}

OF_FASTTEXT static angle_t R_PointToAngleBSP(fixed_t x, fixed_t y)
{
    x -= viewx;
    y -= viewy;

    if (!x && !y)
	return 0;

    if (x >= 0)
    {
	if (y >= 0)
	{
	    if (x > y)
		return tantoangle[R_BspSlopeDiv(y, x)];
	    return ANG90 - 1 - tantoangle[R_BspSlopeDiv(x, y)];
	}

	y = -y;
	if (x > y)
	    return -tantoangle[R_BspSlopeDiv(y, x)];
	return ANG270 + tantoangle[R_BspSlopeDiv(x, y)];
    }

    x = -x;
    if (y >= 0)
    {
	if (x > y)
	    return ANG180 - 1 - tantoangle[R_BspSlopeDiv(y, x)];
	return ANG90 + tantoangle[R_BspSlopeDiv(x, y)];
    }

    y = -y;
    if (x > y)
	return ANG180 + tantoangle[R_BspSlopeDiv(y, x)];
    return ANG270 - 1 - tantoangle[R_BspSlopeDiv(x, y)];
}

static fixed_t __attribute__((noinline)) R_PointToDistBSP(fixed_t x, fixed_t y)
{
    int angle;
    fixed_t dx;
    fixed_t dy;
    fixed_t temp;
    fixed_t frac;

    dx = R_BspAbs(x - viewx);
    dy = R_BspAbs(y - viewy);

    if (dy > dx)
    {
	temp = dx;
	dx = dy;
	dy = temp;
    }

    frac = dx != 0 ? FixedDivPositive(dy, dx) : 0;
    angle = (tantoangle[frac >> DBITS] + ANG90) >> ANGLETOFINESHIFT;

    return FixedDivPositive(dx, finesine[angle]);
}

static angle_t R_VertexViewAngle(vertex_t *vertex)
{
    if (vertex->viewanglevalidcount != validcount)
    {
	vertex->viewangle = R_PointToAngleBSP(vertex->x, vertex->y);
	vertex->viewanglevalidcount = validcount;
    }

    return vertex->viewangle;
}

OF_FASTTEXT fixed_t R_VertexViewDist(vertex_t *vertex)
{
    if (vertex->viewdistvalidcount != validcount)
    {
	vertex->viewdist = R_PointToDistBSP(vertex->x, vertex->y);
	vertex->viewdistvalidcount = validcount;
    }

    return vertex->viewdist;
}

OF_FASTTEXT static angle_t R_BBoxPointAngle(fixed_t x, fixed_t y)
{
    unsigned int hash;
    bbox_angle_cache_t *cache;

    hash = ((unsigned int)x >> 4)
         ^ ((unsigned int)x >> 16)
         ^ ((unsigned int)y >> 7)
         ^ ((unsigned int)y >> 19);
    cache = &bbox_angle_cache[hash & (BBOX_ANGLE_CACHE_SIZE - 1)];

    if (cache->validcount == validcount && cache->x == x && cache->y == y)
	return cache->angle;

    cache->x = x;
    cache->y = y;
    cache->angle = R_PointToAngleBSP(x, y);
    cache->validcount = validcount;

    return cache->angle;
}


void
R_StoreWallRange
( int	start,
  int	stop );




//
// R_ClearDrawSegs
//
void R_ClearDrawSegs (void)
{
    ds_p = drawsegs;
}



//
// ClipWallSegment
// Clips the given range of columns
// and includes it in the new clip list.
//
typedef	struct
{
    int	first;
    int last;
    
} cliprange_t;

// We must expand MAXSEGS to the theoretical limit of the number of solidsegs
// that can be generated in a scene by the DOOM engine. This was determined by
// Lee Killough during BOOM development to be a function of the screensize.
// The simplest thing we can do, other than fix this bug, is to let the game
// render overage and then bomb out by detecting the overflow after the 
// fact. -haleyjd
//#define MAXSEGS 32
#define MAXSEGS (SCREENWIDTH / 2 + 1)

// newend is one past the last valid seg
cliprange_t*	newend;
cliprange_t	solidsegs[MAXSEGS] R_CACHE_ALIGNED;

#define SOLID_COVER_WORDS ((SCREENWIDTH + 31) / 32)

// Exact screen-column coverage for solid wall segments.  The solidsegs
// list still owns wall clipping; this is only a faster bbox coverage test.
static uint32_t solidcovered[SOLID_COVER_WORDS] R_CACHE_ALIGNED;

static int R_SolidSegsFull(void)
{
    return solidsegs[0].last >= viewwidth;
}

static inline void R_MarkSolidColumns(int first, int last)
{
    int firstword;
    int lastword;
    uint32_t firstmask;
    uint32_t lastmask;

    if (first < 0)
	first = 0;
    if (last >= viewwidth)
	last = viewwidth - 1;
    if (first > last)
	return;

    firstword = first >> 5;
    lastword = last >> 5;
    firstmask = 0xffffffffu << (first & 31);
    lastmask = 0xffffffffu >> (31 - (last & 31));

    if (firstword == lastword)
    {
	solidcovered[firstword] |= firstmask & lastmask;
	return;
    }

    solidcovered[firstword] |= firstmask;
    for (int word = firstword + 1; word < lastword; word++)
	solidcovered[word] = 0xffffffffu;
    solidcovered[lastword] |= lastmask;
}

static inline boolean R_SolidColumnsCovered(int first, int last)
{
    int firstword;
    int lastword;
    uint32_t firstmask;
    uint32_t lastmask;

    if (first < 0)
	first = 0;
    if (last >= viewwidth)
	last = viewwidth - 1;
    if (first > last)
	return false;

    firstword = first >> 5;
    lastword = last >> 5;
    firstmask = 0xffffffffu << (first & 31);
    lastmask = 0xffffffffu >> (31 - (last & 31));

    if (firstword == lastword)
    {
	uint32_t mask = firstmask & lastmask;
	return (solidcovered[firstword] & mask) == mask;
    }

    if ((solidcovered[firstword] & firstmask) != firstmask)
	return false;

    for (int word = firstword + 1; word < lastword; word++)
    {
	if (solidcovered[word] != 0xffffffffu)
	    return false;
    }

    return (solidcovered[lastword] & lastmask) == lastmask;
}

static cliprange_t *R_FindClipRange(int last)
{
    cliprange_t *lo = solidsegs;
    cliprange_t *hi = newend;

    while (lo < hi)
    {
	cliprange_t *mid = lo + ((hi - lo) >> 1);

	if (mid->last < last)
	    lo = mid + 1;
	else
	    hi = mid;
    }

    return lo;
}




//
// R_ClipSolidWallSegment
// Does handle solid walls,
//  e.g. single sided LineDefs (middle texture)
//  that entirely block the view.
// 
OF_FASTTEXT void
R_ClipSolidWallSegment
( int			first,
  int			last )
{
    cliprange_t*	next;
    cliprange_t*	start;

    R_MarkSolidColumns(first, last);

    // Find the first range that touches the range
    //  (adjacent pixels are touching).
    start = R_FindClipRange(first - 1);

    if (first < start->first)
    {
	if (last < start->first-1)
	{
	    // Post is entirely visible (above start),
	    //  so insert a new clippost.
	    R_StoreWallRange (first, last);
	    if (newend >= &solidsegs[MAXSEGS])
		return;

	    next = newend;
	    newend++;
	    
	    while (next != start)
	    {
		*next = *(next-1);
		next--;
	    }
	    next->first = first;
	    next->last = last;
	    return;
	}
		
	// There is a fragment above *start.
	R_StoreWallRange (first, start->first - 1);
	// Now adjust the clip size.
	start->first = first;	
    }

    // Bottom contained in start?
    if (last <= start->last)
	return;			
		
    next = start;
    while (last >= (next+1)->first-1)
    {
	// There is a fragment between two posts.
	R_StoreWallRange (next->last + 1, (next+1)->first - 1);
	next++;
	
	if (last <= next->last)
	{
	    // Bottom is contained in next.
	    // Adjust the clip size.
	    start->last = next->last;	
	    goto crunch;
	}
    }
	
    // There is a fragment after *next.
    R_StoreWallRange (next->last + 1, last);
    // Adjust the clip size.
    start->last = last;
	
    // Remove start+1 to next from the clip list,
    // because start now covers their area.
  crunch:
    if (next == start)
    {
	// Post just extended past the bottom of one post.
	return;
    }
    

    {
	cliprange_t *dst = start + 1;
	cliprange_t *src = next + 1;

	while (src < newend)
	    *dst++ = *src++;

	newend = dst;
    }
}



//
// R_ClipPassWallSegment
// Clips the given range of columns,
//  but does not includes it in the clip list.
// Does handle windows,
//  e.g. LineDefs with upper and lower texture.
//
OF_FASTTEXT void
R_ClipPassWallSegment
( int	first,
  int	last )
{
    cliprange_t*	start;

    // Find the first range that touches the range
    //  (adjacent pixels are touching).
    start = R_FindClipRange(first - 1);

    if (first < start->first)
    {
	if (last < start->first-1)
	{
	    // Post is entirely visible (above start).
	    R_StoreWallRange (first, last);
	    return;
	}
		
	// There is a fragment above *start.
	R_StoreWallRange (first, start->first - 1);
    }

    // Bottom contained in start?
    if (last <= start->last)
	return;			
		
    while (last >= (start+1)->first-1)
    {
	// There is a fragment between two posts.
	R_StoreWallRange (start->last + 1, (start+1)->first - 1);
	start++;
	
	if (last <= start->last)
	    return;
    }
	
    // There is a fragment after *next.
    R_StoreWallRange (start->last + 1, last);
}



//
// R_ClearClipSegs
//
void R_ClearClipSegs (void)
{
    solidsegs[0].first = -0x7fffffff;
    solidsegs[0].last = -1;
    solidsegs[1].first = viewwidth;
    solidsegs[1].last = 0x7fffffff;
    newend = solidsegs+2;
    memset(solidcovered, 0, sizeof(solidcovered));
}

//
// R_AddLine
// Clips the given segment
// and adds any visible pieces to the line list.
//
OF_FASTTEXT void R_AddLine (seg_t*	line)
{
    int			x1;
    int			x2;
    angle_t		angle1;
    angle_t		angle2;
    angle_t		span;
    angle_t		tspan;
    unsigned int        perf_start;

    perf_start = R_PERF_DETAIL_BEGIN();
    
    curline = line;
    cursegcache = &rendersegcache[line - segs];
    backsector = cursegcache->backsector;

    if (backsector
	&& backsector->ceilingheight > frontsector->floorheight
	&& backsector->floorheight < frontsector->ceilingheight
	&& backsector->ceilingheight == frontsector->ceilingheight
	&& backsector->floorheight == frontsector->floorheight
	&& backsector->ceilingpic == frontsector->ceilingpic
	&& backsector->floorpic == frontsector->floorpic
	&& backsector->lightlevel == frontsector->lightlevel
	&& cursegcache->sidedef->midtexture == 0)
    {
	goto done;
    }

    // OPTIMIZE: quickly reject orthogonal back sides.
    angle1 = R_VertexViewAngle(cursegcache->v1);
    angle2 = R_VertexViewAngle(cursegcache->v2);
    
    // Clip to view edges.
    // OPTIMIZE: make constant out of 2*clipangle (FIELDOFVIEW).
    span = angle1 - angle2;
    
    // Back side? I.e. backface culling?
    if (span >= ANG180)
	goto done;

    // Global angle needed by segcalc.
    rw_angle1 = angle1;
    angle1 -= viewangle;
    angle2 -= viewangle;
	
    tspan = angle1 + clipangle;
    if (tspan > 2*clipangle)
    {
	tspan -= 2*clipangle;

	// Totally off the left edge?
	if (tspan >= span)
	    goto done;
	
	angle1 = clipangle;
    }
    tspan = clipangle - angle2;
    if (tspan > 2*clipangle)
    {
	tspan -= 2*clipangle;

	// Totally off the left edge?
	if (tspan >= span)
	    goto done;
	angle2 = -clipangle;
    }
    
    // The seg is in the view range,
    // but not necessarily visible.
    angle1 = (angle1+ANG90)>>ANGLETOFINESHIFT;
    angle2 = (angle2+ANG90)>>ANGLETOFINESHIFT;
    x1 = viewangletox[angle1];
    x2 = viewangletox[angle2];

    // Does not cross a pixel.  Interpolated views can quantize a near-edge
    // seg to an empty/reversed screen span; do not pass that to clipping.
    if (x1 >= x2)
	goto done;
	
    // Single sided line?
    if (!backsector)
	goto clipsolid;		

    // Closed door.
    if (backsector->ceilingheight <= frontsector->floorheight
	|| backsector->floorheight >= frontsector->ceilingheight)
	goto clipsolid;		

    // Window.
    if (backsector->ceilingheight != frontsector->ceilingheight
	|| backsector->floorheight != frontsector->floorheight)
	goto clippass;	
		
    // Reject empty lines used for triggers
    //  and special events.
    // Identical floor and ceiling on both sides,
    // identical light levels on both sides,
    // and no middle texture.
    if (backsector->ceilingpic == frontsector->ceilingpic
	&& backsector->floorpic == frontsector->floorpic
	&& backsector->lightlevel == frontsector->lightlevel
	&& cursegcache->sidedef->midtexture == 0)
    {
	goto done;
    }
    
				
  clippass:
    R_ClipPassWallSegment (x1, x2-1);	
    goto done;
		
  clipsolid:
    R_ClipSolidWallSegment (x1, x2-1);

  done:
    R_PERF_DETAIL_END(R_PERF_DETAIL_BSP_ADD_LINE, perf_start);
}


//
// R_CheckBBox
// Checks BSP node/subtree bounding box.
// Returns true
//  if some part of the bbox might be visible.
//
static const unsigned char checkcoord[12][4] =
{
    {3,0,2,1},
    {3,0,2,0},
    {3,1,2,0},
    {0},
    {2,0,2,1},
    {0,0,0,0},
    {3,1,3,0},
    {0},
    {2,0,3,1},
    {2,1,3,1},
    {2,1,3,0}
};


OF_FASTTEXT boolean R_CheckBBox (fixed_t*	bspcoord)
{
    int			boxx;
    int			boxy;
    int			boxpos;

    fixed_t		x1;
    fixed_t		y1;
    fixed_t		x2;
    fixed_t		y2;
    
    angle_t		angle1;
    angle_t		angle2;
    angle_t		span;
    angle_t		tspan;
    angle_t		doubleclip;
    const unsigned char* coords;
    
    cliprange_t*	start;

    int			sx1;
    int			sx2;
    boolean             result;
    unsigned int        perf_start;

    perf_start = R_PERF_DETAIL_BEGIN();
    
    // Find the corners of the box
    // that define the edges from current viewpoint.
    if (viewx <= bspcoord[BOXLEFT])
	boxx = 0;
    else if (viewx < bspcoord[BOXRIGHT])
	boxx = 1;
    else
	boxx = 2;
		
    if (viewy >= bspcoord[BOXTOP])
	boxy = 0;
    else if (viewy > bspcoord[BOXBOTTOM])
	boxy = 1;
    else
	boxy = 2;
		
    boxpos = (boxy<<2)+boxx;
    if (boxpos == 5)
    {
	result = true;
	goto done;
    }
	
    coords = checkcoord[boxpos];
    x1 = bspcoord[coords[0]];
    y1 = bspcoord[coords[1]];
    x2 = bspcoord[coords[2]];
    y2 = bspcoord[coords[3]];
    
    // check clip list for an open space
    angle1 = R_BBoxPointAngle (x1, y1) - viewangle;
    angle2 = R_BBoxPointAngle (x2, y2) - viewangle;
	
    span = angle1 - angle2;

    // Sitting on a line?
    if (span >= ANG180)
    {
	result = true;
	goto done;
    }
    
    doubleclip = clipangle << 1;
    tspan = angle1 + clipangle;

    if (tspan > doubleclip)
    {
	tspan -= doubleclip;

	// Totally off the left edge?
	if (tspan >= span)
	{
	    result = false;
	    goto done;
	}

	angle1 = clipangle;
    }
    tspan = clipangle - angle2;
    if (tspan > doubleclip)
    {
	tspan -= doubleclip;

	// Totally off the left edge?
	if (tspan >= span)
	{
	    result = false;
	    goto done;
	}
	
	angle2 = -clipangle;
    }


    // Find the first clippost
    //  that touches the source post
    //  (adjacent pixels are touching).
    angle1 = (angle1+ANG90)>>ANGLETOFINESHIFT;
    angle2 = (angle2+ANG90)>>ANGLETOFINESHIFT;
    sx1 = viewangletox[angle1];
    sx2 = viewangletox[angle2];

    // Does not cross a pixel.  Vanilla only rejects equal endpoints here;
    // wrapped/reversed bbox spans still need the normal clip-list test.
    if (sx1 == sx2)
    {
	result = false;
	goto done;
    }
    sx2--;

    if (sx1 <= sx2)
    {
	result = !R_SolidColumnsCovered(sx1, sx2);
	goto done;
    }
	
    // Wrapped/reversed bbox spans keep the original clip-list path.
    start = R_FindClipRange(sx2);
    
    if (sx1 >= start->first
	&& sx2 <= start->last)
    {
	// The clippost contains the new span.
	result = false;
	goto done;
    }

    result = true;

  done:
    R_PERF_DETAIL_END(R_PERF_DETAIL_BSP_CHECK_BBOX, perf_start);
    return result;
}



//
// R_Subsector
// Determine floor/ceiling planes.
// Add sprites of things in sector.
// Draw one or more line segments.
//
OF_FASTTEXT void R_Subsector (int num)
{
    int			count;
    seg_t*		line;
    subsector_t*	sub;
    sector_plane_cache_t* plane_cache;
    int                 sector_index;
    unsigned int        perf_start;

    perf_start = R_PERF_DETAIL_BEGIN();
	
#ifdef RANGECHECK
    if (num>=numsubsectors)
	I_Error ("R_Subsector: ss %i with numss = %i",
		 num,
		 numsubsectors);
#endif

    sscount++;
    sub = &subsectors[num];
    frontsector = sub->sector;
    count = sub->numlines;
    line = &segs[sub->firstline];

    sector_index = frontsector - sectors;
    plane_cache = sector_plane_cache != NULL
               && (unsigned)sector_index < (unsigned)numsectors
                ? &sector_plane_cache[sector_index] : NULL;

    if (plane_cache != NULL && plane_cache->validcount == validcount)
    {
	floorplane = plane_cache->floorplane;
	ceilingplane = plane_cache->ceilingplane;
    }
    else
    {
	if (frontsector->floorheight < viewz)
	{
	    floorplane = R_FindPlane (frontsector->floorheight,
				      frontsector->floorpic,
				      frontsector->lightlevel);
	}
	else
	    floorplane = NULL;

	if (frontsector->ceilingheight > viewz
	    || frontsector->ceilingpic == skyflatnum)
	{
	    ceilingplane = R_FindPlane (frontsector->ceilingheight,
					frontsector->ceilingpic,
					frontsector->lightlevel);
	}
	else
	    ceilingplane = NULL;

	if (plane_cache != NULL)
	{
	    plane_cache->floorplane = floorplane;
	    plane_cache->ceilingplane = ceilingplane;
	    plane_cache->validcount = validcount;
	}
    }
		
    R_AddSprites (frontsector);	

    while (count--)
    {
	R_AddLine (line);
	line++;
    }

    // check for solidsegs overflow - extremely unsatisfactory!
    if (newend > &solidsegs[MAXSEGS])
        I_Error("R_Subsector: solidsegs overflow\n");

    R_PERF_DETAIL_END(R_PERF_DETAIL_BSP_SUBSECTOR, perf_start);
}




//
// RenderBSPNode
// Renders all subsectors below a given node,
//  traversing subtree recursively.
// Just call with BSP root.
//
#define BSP_STACK_MAX 128

typedef struct
{
    int backchild;
    fixed_t *backbbox;
    unsigned char contains_view;
} bsp_stack_t;

OF_FASTTEXT void R_RenderBSPNode (int bspnum)
{
    bsp_render_node_t*	bsp;
    int		side;
    int		frontchild;
    int		backchild;
    bsp_stack_t	stack[BSP_STACK_MAX];
    int		stack_top = 0;
    int		found_back;
    int		contains_view = 1;

    if (bsp_render_nodes == NULL && numnodes > 0)
	R_BuildBSPRenderData();

    for (;;)
    {
	if (R_SolidSegsFull())
	    return;

	while (!(bspnum & NF_SUBSECTOR))
	{
	    if (R_SolidSegsFull())
		return;

	    R_PERF_DETAIL_COUNT(R_PERF_DETAIL_BSP_NODE);

	    bsp = &bsp_render_nodes[bspnum];

	    // Decide which side the view point is on.
	    side = R_PointOnSideBSP(bsp);
	    frontchild = bsp->children[side];
	    backchild = bsp->children[side^1];

	    // Once traversal moves into a subtree that cannot contain the
	    // viewpoint, both children can be rejected by the bbox clip test.
	    if (!contains_view)
	    {
		R_PERF_DETAIL_COUNT(R_PERF_DETAIL_BSP_FRONT_BBOX);
		if (!R_CheckBBox(bsp->bbox[side]))
		{
		    R_PERF_DETAIL_COUNT(R_PERF_DETAIL_BSP_FRONT_CULLED);
		    if (R_CheckBBox(bsp->bbox[side^1]))
		    {
			bspnum = backchild;
			continue;
		    }
		    goto pop_back_child;
		}
	    }

	    // Render front space first.  The back-child bbox must be checked
	    // after front space updates solidsegs, so keep the parent pending.
	    if (stack_top == BSP_STACK_MAX)
	    {
		R_RenderBSPNode (frontchild);
		if (R_CheckBBox (bsp->bbox[side^1]))
		{
		    bspnum = backchild;
		    contains_view = 0;
		    continue;
		}
		goto pop_back_child;
	    }

	    stack[stack_top].backchild = backchild;
	    stack[stack_top].backbbox = bsp->bbox[side^1];
	    stack[stack_top].contains_view = 0;
	    stack_top++;
	    bspnum = frontchild;
	}

	R_PERF_DETAIL_COUNT(R_PERF_DETAIL_BSP_NODE);

	// Found a subsector.
	if (bspnum == -1)
	    R_Subsector (0);
	else
	    R_Subsector (bspnum&(~NF_SUBSECTOR));

	if (R_SolidSegsFull())
	    return;

      pop_back_child:
	found_back = 0;
	while (stack_top > 0)
	{
	    bsp_stack_t *entry;

	    stack_top--;
	    entry = &stack[stack_top];

	    // Possibly divide back space.
	    if (R_CheckBBox (entry->backbbox))
	    {
		bspnum = entry->backchild;
		contains_view = entry->contains_view;
		found_back = 1;
		break;
	    }
	}

	if (!found_back)
	    return;
    }
}
