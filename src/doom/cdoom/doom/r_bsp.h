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
//	Refresh module, BSP traversal and handling.
//


#ifndef __R_BSP__
#define __R_BSP__

#include "r_defs.h"


extern seg_t*		curline;
extern side_t*		sidedef;
extern line_t*		linedef;
extern sector_t*	frontsector;
extern sector_t*	backsector;

extern int		rw_x;
extern int		rw_stopx;

extern boolean		segtextured;

// false if the back side is the same plane
extern boolean		markfloor;		
extern boolean		markceiling;

extern boolean		skymap;

extern drawseg_t	drawsegs[MAXDRAWSEGS];
extern drawseg_t*	ds_p;

extern lighttable_t**	hscalelight;
extern lighttable_t**	vscalelight;
extern lighttable_t**	dscalelight;

typedef struct
{
    vertex_t*   v1;
    vertex_t*   v2;
    sector_t*   frontsector;
    sector_t*   backsector;
    side_t*     sidedef;
    line_t*     linedef;
    angle_t     angle;
    angle_t     normalangle;
    fixed_t     offset;
    short       pegflags;
    signed char lightbias;
    byte        pad[1];
} rendersegcache_t;

extern rendersegcache_t* rendersegcache;
extern rendersegcache_t* cursegcache;


typedef void (*drawfunc_t) (int start, int stop);


// BSP?
void R_ClearClipSegs (void);
void R_ClearDrawSegs (void);
void R_BuildBSPRenderData (void);
void R_BuildSegRenderData (void);
void R_UpdateSegRenderData (void);
void R_UpdateSectorPlaneCache (sector_t *sector,
			       visplane_t *floor,
			       visplane_t *ceiling,
			       boolean update_floor,
			       boolean update_ceiling);

fixed_t R_VertexViewDist(vertex_t *vertex);

void R_RenderBSPNode (int bspnum);


#endif
