/* Check camera-position cache reuse, invalidation and generation rollover. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "of_fastram.h"
#undef OF_FASTTEXT
#define OF_FASTTEXT __attribute__((noinline))
#ifndef DOOM_BSP_SOURCE
#define DOOM_BSP_SOURCE "../../src/doom/cdoom/doom/r_bsp.c"
#endif
#include DOOM_BSP_SOURCE

fixed_t viewx, viewy, viewz;
angle_t viewangle;
int validcount = 1, viewwidth = 320;
int numvertexes, numnodes;
vertex_t *vertexes;
node_t *nodes;

int M_CheckParm(const char *arg) { return 1; }
void *Z_Malloc(int size, int tag, void *ptr) { abort(); }

static vertex_t points[] = {
    {.x = 64 * FRACUNIT, .y = 32 * FRACUNIT},
    {.x = -96 * FRACUNIT, .y = 80 * FRACUNIT},
    {.x = 40 * FRACUNIT, .y = -128 * FRACUNIT}
};

static void check_points(void)
{
    for (int i = 0; i < numvertexes; ++i) {
        vertex_t *v = &vertexes[i];
        angle_t angle = R_PointToAngleBSP(v->x, v->y);
        fixed_t distance = R_PointToDistBSP(v->x, v->y);
        assert(R_VertexViewAngle(v) == angle);
        assert(R_VertexViewDist(v) == distance);
        assert(R_BBoxPointAngle(v->x, v->y) == angle);
    }
}

int main(void)
{
    static bbox_angle_cache_t saved_bbox[BBOX_ANGLE_CACHE_SIZE];
    vertex_t saved_points[3];
    vertexes = points;
    numvertexes = sizeof(points) / sizeof(points[0]);
    R_BuildBSPRenderData();
    R_ClearClipSegs();
    check_points();
    int stamp = bsp_view_validcount;
    memcpy(saved_bbox, bbox_angle_cache, sizeof(saved_bbox));
    memcpy(saved_points, points, sizeof(points));

    /* Turning, bobbing and gameplay's unrelated validity counter changes. */
    for (int i = 0; i < 1000; ++i) {
        viewangle += ANG45;
        viewz = (i & 31) * FRACUNIT;
        validcount += 7;
        R_ClearClipSegs();
        check_points();
        assert(bsp_view_validcount == stamp);
        assert(!memcmp(saved_bbox, bbox_angle_cache, sizeof(saved_bbox)));
        assert(!memcmp(saved_points, points, sizeof(points)));
    }

    /* Include a one-unit interpolated movement and return to the old position. */
    static const fixed_t positions[][2] = {
        {1, 0}, {0, 1}, {11 * FRACUNIT, -7 * FRACUNIT}, {0, 0}
    };
    for (unsigned i = 0; i < sizeof(positions) / sizeof(positions[0]); ++i) {
        viewx = positions[i][0];
        viewy = positions[i][1];
        R_ClearClipSegs();
        assert(bsp_view_validcount != stamp);
        stamp = bsp_view_validcount;
        check_points();
    }

    /* Same camera coordinates on a new level still invalidate old entries. */
    R_BuildBSPRenderData();
    R_ClearClipSegs();
    assert(bsp_view_validcount != stamp);
    check_points();

    /* A reused generation must not revive entries left from that generation. */
    bsp_view_validcount = INT_MAX;
    for (int i = 0; i < numvertexes; ++i) {
        points[i].viewanglevalidcount = points[i].viewdistvalidcount = 1;
        points[i].viewangle = 123;
        points[i].viewdist = 456;
    }
    for (int i = 0; i < BBOX_ANGLE_CACHE_SIZE; ++i) {
        bbox_angle_cache[i].validcount = 1;
        bbox_angle_cache[i].angle = 123;
    }
    ++viewx;
    R_ClearClipSegs();
    assert(bsp_view_validcount == 1);
    check_points();

    /* More coordinates than cache slots exercise replacement collisions. */
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < BBOX_ANGLE_CACHE_SIZE * 2; ++i) {
            fixed_t x = (i - 4096) * FRACUNIT;
            fixed_t y = (i % 19 - 9) * FRACUNIT;
            assert(R_BBoxPointAngle(x, y) == R_PointToAngleBSP(x, y));
        }
    puts("PASS: turning/bobbing, movement, level change, rollover, collisions");
    return 0;
}
