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
//  Refresh module, data I/O, caching, retrieval of graphics
//  by name.
//


#ifndef __R_DATA__
#define __R_DATA__

#include "r_defs.h"
#include "r_state.h"


// Retrieve column data for span blitting.
byte*
R_GetColumn
( int		tex,
  int		col );

byte **R_GetColumnTable(int tex);
int R_GetTextureWidthMask(int tex);

// Contiguous column-major 2D block for the GPU param-wall path
// (column x at block + x*texheight).  NULL when over the per-level
// cache budget — caller falls back to column emission.
byte *R_GetWallTexture2D(int texnum);

// Flat 2D sprite block (column-major, stride = patch height) for the
// GPU affine-sprite path.  spritelump is vis->patch (relative index).
byte *R_GetSpriteTexture2D(int spritelump);

// Post-aware 2D block for the GPU param-masked-midtexture path.
byte *R_GetMaskedTexture2D(int texnum);


// I/O, setting up the stuff.
void R_InitData (void);
void R_PrecacheLevel (void);


// Retrieval.
// Floor/ceiling opaque texture tiles,
// lookup by name. For animation?
int R_FlatNumForName(const char *name);
byte *R_GetFlatData(int flatnum, boolean permanent);


// Called by P_Ticker for switches and animations,
// returns the texture number for the texture name.
int R_TextureNumForName(const char *name);
int R_CheckTextureNumForName(const char *name);


extern int numflats;
extern int numtextures;


#endif
