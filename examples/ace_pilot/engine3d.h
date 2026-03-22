/*
 * engine3d.h - 3D wireframe engine for Ace Pilot
 * Fixed-point math, camera transform, perspective projection
 */
#ifndef ENGINE3D_H
#define ENGINE3D_H

#include <exec/types.h>

/* Fixed-point 16.16 */
typedef LONG Fixed;
#define FIX(x)       ((Fixed)((x) * 65536L))
#define FIX_INT(x)   ((x) >> 16)
#define FIX_MUL(a,b) ((Fixed)(((LONG)(a) >> 8) * ((LONG)(b) >> 8)))

/* Angle: 0..255 -> 0..360 degrees */
#define ANGLE_COUNT 256

extern Fixed sin_tab[ANGLE_COUNT];
extern Fixed cos_tab[ANGLE_COUNT];

void engine3d_init(void);

/* 3D vector (integer world coords) */
typedef struct { LONG x, y, z; } Vec3;

/* Camera */
typedef struct {
    Vec3 pos;
    WORD pitch;  /* 0-255 */
    WORD yaw;    /* 0-255 */
} Camera;

/* Viewport (screen region to render into) */
typedef struct {
    WORD x, y, w, h;
} Viewport;

/* Transform world point to camera-relative coords.
 * Returns camera-relative Z (depth). Negative = behind camera. */
LONG transform_point(const Camera *cam, LONG wx, LONG wy, LONG wz,
                     LONG *cx, LONG *cy, LONG *cz);

/* Project camera-relative point to screen coords in viewport.
 * Returns 1 if visible (in front of near clip), 0 otherwise. */
WORD project_point(LONG cx, LONG cy, LONG cz,
                   const Viewport *vp, WORD *sx, WORD *sy);

/* Cohen-Sutherland 2D line clip to rectangle.
 * Modifies coords in-place. Returns 1 if any part visible. */
WORD clip_line(WORD *x1, WORD *y1, WORD *x2, WORD *y2,
               WORD left, WORD top, WORD right, WORD bottom);

/* Rotate point around Y axis */
void rotate_y(LONG x, LONG y, LONG z, WORD angle,
              LONG *ox, LONG *oy, LONG *oz);

/* Rotate point around X axis */
void rotate_x(LONG x, LONG y, LONG z, WORD angle,
              LONG *ox, LONG *oy, LONG *oz);

#define FOV_SCALE  200
#define NEAR_CLIP  20
#define FAR_CLIP   4000

#endif
