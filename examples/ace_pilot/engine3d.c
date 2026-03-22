/*
 * engine3d.c - 3D math, camera transform, perspective projection
 */
#include "engine3d.h"

Fixed sin_tab[ANGLE_COUNT];
Fixed cos_tab[ANGLE_COUNT];

/* Precomputed sine table: sin(i * 2*PI / 256) * 65536 */
static const Fixed sin_precomp[256] = {
        0,   1608,   3216,   4821,   6424,   8022,   9616,  11204,
    12785,  14359,  15924,  17479,  19024,  20557,  22078,  23586,
    25080,  26558,  28020,  29466,  30893,  32303,  33692,  35062,
    36410,  37736,  39040,  40320,  41576,  42806,  44011,  45190,
    46341,  47464,  48559,  49624,  50660,  51665,  52639,  53581,
    54491,  55368,  56212,  57022,  57798,  58538,  59244,  59914,
    60547,  61145,  61705,  62228,  62714,  63162,  63572,  63944,
    64277,  64571,  64827,  65043,  65220,  65358,  65457,  65516,
    65536,  65516,  65457,  65358,  65220,  65043,  64827,  64571,
    64277,  63944,  63572,  63162,  62714,  62228,  61705,  61145,
    60547,  59914,  59244,  58538,  57798,  57022,  56212,  55368,
    54491,  53581,  52639,  51665,  50660,  49624,  48559,  47464,
    46341,  45190,  44011,  42806,  41576,  40320,  39040,  37736,
    36410,  35062,  33692,  32303,  30893,  29466,  28020,  26558,
    25080,  23586,  22078,  20557,  19024,  17479,  15924,  14359,
    12785,  11204,   9616,   8022,   6424,   4821,   3216,   1608,
        0,  -1608,  -3216,  -4821,  -6424,  -8022,  -9616, -11204,
   -12785, -14359, -15924, -17479, -19024, -20557, -22078, -23586,
   -25080, -26558, -28020, -29466, -30893, -32303, -33692, -35062,
   -36410, -37736, -39040, -40320, -41576, -42806, -44011, -45190,
   -46341, -47464, -48559, -49624, -50660, -51665, -52639, -53581,
   -54491, -55368, -56212, -57022, -57798, -58538, -59244, -59914,
   -60547, -61145, -61705, -62228, -62714, -63162, -63572, -63944,
   -64277, -64571, -64827, -65043, -65220, -65358, -65457, -65516,
   -65536, -65516, -65457, -65358, -65220, -65043, -64827, -64571,
   -64277, -63944, -63572, -63162, -62714, -62228, -61705, -61145,
   -60547, -59914, -59244, -58538, -57798, -57022, -56212, -55368,
   -54491, -53581, -52639, -51665, -50660, -49624, -48559, -47464,
   -46341, -45190, -44011, -42806, -41576, -40320, -39040, -37736,
   -36410, -35062, -33692, -32303, -30893, -29466, -28020, -26558,
   -25080, -23586, -22078, -20557, -19024, -17479, -15924, -14359,
   -12785, -11204,  -9616,  -8022,  -6424,  -4821,  -3216,  -1608
};

void engine3d_init(void)
{
    int i;
    for (i = 0; i < 256; i++) {
        sin_tab[i] = sin_precomp[i];
        cos_tab[i] = sin_precomp[(i + 64) & 255];
    }
}

void rotate_y(LONG x, LONG y, LONG z, WORD angle,
              LONG *ox, LONG *oy, LONG *oz)
{
    Fixed c = cos_tab[angle & 255];
    Fixed s = sin_tab[angle & 255];
    *ox = (x * c - z * s) >> 16;
    *oy = y;
    *oz = (x * s + z * c) >> 16;
}

void rotate_x(LONG x, LONG y, LONG z, WORD angle,
              LONG *ox, LONG *oy, LONG *oz)
{
    Fixed c = cos_tab[angle & 255];
    Fixed s = sin_tab[angle & 255];
    *ox = x;
    *oy = (y * c - z * s) >> 16;
    *oz = (y * s + z * c) >> 16;
}

LONG transform_point(const Camera *cam, LONG wx, LONG wy, LONG wz,
                     LONG *cx, LONG *cy, LONG *cz)
{
    LONG dx, dy, dz;
    LONG rx, ry, rz;
    Fixed cyaw, syaw, cpitch, spitch;

    dx = wx - cam->pos.x;
    dy = wy - cam->pos.y;
    dz = wz - cam->pos.z;

    /* Rotate by inverse yaw around Y axis */
    cyaw = cos_tab[cam->yaw & 255];
    syaw = sin_tab[cam->yaw & 255];
    rx = (dx * cyaw - dz * syaw) >> 16;
    rz = (dx * syaw + dz * cyaw) >> 16;
    ry = dy;

    /* Rotate by -pitch around X axis */
    cpitch = cos_tab[cam->pitch & 255];
    spitch = sin_tab[cam->pitch & 255];
    *cx = rx;
    *cy = (ry * cpitch + rz * spitch) >> 16;
    *cz = (-ry * spitch + rz * cpitch) >> 16;

    return *cz;
}

WORD project_point(LONG cx, LONG cy, LONG cz,
                   const Viewport *vp, WORD *sx, WORD *sy)
{
    LONG px, py;

    if (cz < NEAR_CLIP || cz > FAR_CLIP) return 0;

    px = (LONG)(vp->x + vp->w / 2) + (cx * FOV_SCALE) / cz;
    py = (LONG)(vp->y + vp->h / 2) - (cy * FOV_SCALE) / cz;

    /* Clamp to safe WORD range for clipping */
    if (px < -500 || px > 820 || py < -500 || py > 760) return 0;

    *sx = (WORD)px;
    *sy = (WORD)py;
    return 1;
}

/* Cohen-Sutherland line clipping */
#define CS_LEFT   1
#define CS_RIGHT  2
#define CS_BOTTOM 4
#define CS_TOP    8

static WORD cs_code(WORD x, WORD y,
                    WORD left, WORD top, WORD right, WORD bottom)
{
    WORD code = 0;
    if (x < left)   code |= CS_LEFT;
    if (x > right)  code |= CS_RIGHT;
    if (y < top)    code |= CS_TOP;
    if (y > bottom) code |= CS_BOTTOM;
    return code;
}

WORD clip_line(WORD *x1, WORD *y1, WORD *x2, WORD *y2,
               WORD left, WORD top, WORD right, WORD bottom)
{
    WORD c1, c2, iter;

    c1 = cs_code(*x1, *y1, left, top, right, bottom);
    c2 = cs_code(*x2, *y2, left, top, right, bottom);

    for (iter = 0; iter < 10; iter++) {
        WORD c, x, y;
        LONG dx, dy;

        if (!(c1 | c2)) return 1;  /* both inside */
        if (c1 & c2)    return 0;  /* both outside same side */

        c = c1 ? c1 : c2;
        dx = (LONG)*x2 - (LONG)*x1;
        dy = (LONG)*y2 - (LONG)*y1;

        if (c & CS_LEFT) {
            if (dx == 0) return 0;
            y = (WORD)((LONG)*y1 + dy * (LONG)(left - *x1) / dx);
            x = left;
        } else if (c & CS_RIGHT) {
            if (dx == 0) return 0;
            y = (WORD)((LONG)*y1 + dy * (LONG)(right - *x1) / dx);
            x = right;
        } else if (c & CS_TOP) {
            if (dy == 0) return 0;
            x = (WORD)((LONG)*x1 + dx * (LONG)(top - *y1) / dy);
            y = top;
        } else {
            if (dy == 0) return 0;
            x = (WORD)((LONG)*x1 + dx * (LONG)(bottom - *y1) / dy);
            y = bottom;
        }

        if (c == c1) {
            *x1 = x; *y1 = y;
            c1 = cs_code(x, y, left, top, right, bottom);
        } else {
            *x2 = x; *y2 = y;
            c2 = cs_code(x, y, left, top, right, bottom);
        }
    }
    return 0;
}
