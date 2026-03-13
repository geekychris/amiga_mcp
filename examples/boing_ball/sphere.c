#include "sphere.h"
#include "tables.h"

#include <proto/graphics.h>
#include <proto/exec.h>

SphereFrame sphere_frames[NUM_DEPTHS][NUM_FRAMES];

/* Render radii: near (large) to far (small), step ~3px */
static const WORD depth_radii[NUM_DEPTHS] = {
    44, 41, 38, 35, 32, 29, 26, 23
};

/* Color palette indices */
#define COL_RED_BASE    1
#define COL_WHITE_BASE  8
#define COL_SPECULAR   15

/* Light from upper-left-front */
#define LIGHT_X  (-160)
#define LIGHT_Y  (-200)
#define LIGHT_Z   100

/* Halfway vector for specular */
#define HALF_X  (-84)
#define HALF_Y  (-105)
#define HALF_Z   218

static void set_pixel(struct BitMap *bm, WORD x, WORD y, UBYTE color)
{
    WORD plane;
    WORD byte_offset = (y * bm->BytesPerRow) + (x >> 3);
    UBYTE bit = 0x80 >> (x & 7);

    for (plane = 0; plane < bm->Depth; plane++) {
        if (color & (1 << plane))
            bm->Planes[plane][byte_offset] |= bit;
        else
            bm->Planes[plane][byte_offset] &= ~bit;
    }
}

static void set_mask_pixel(struct BitMap *mask, WORD x, WORD y)
{
    WORD byte_offset = (y * mask->BytesPerRow) + (x >> 3);
    mask->Planes[0][byte_offset] |= (0x80 >> (x & 7));
}

static void render_frame(WORD depth_idx, WORD frame_idx)
{
    SphereFrame *sf = &sphere_frames[depth_idx][frame_idx];
    WORD render_r = depth_radii[depth_idx];
    WORD cx = SPHERE_SIZE / 2;
    WORD cy = SPHERE_SIZE / 2;
    WORD x, y;
    LONG r2 = (LONG)render_r * render_r;
    WORD rot_offset = (frame_idx * TABLE_SIZE) / NUM_FRAMES;

    for (y = 0; y < SPHERE_SIZE; y++) {
        WORD dy = y - cy;
        LONG dy2 = (LONG)dy * dy;

        for (x = 0; x < SPHERE_SIZE; x++) {
            WORD dx = x - cx;
            LONG dx2 = (LONG)dx * dx;
            LONG dist2 = dx2 + dy2;
            LONG nz_fp, nx_fp, ny_fp;
            LONG dot, diffuse;
            WORD shade;
            UBYTE color;
            WORD u, v, checker;

            if (dist2 >= r2)
                continue;

            set_mask_pixel(sf->mask, x, y);

            nx_fp = FP_DIV(INT_TO_FP(dx), INT_TO_FP(render_r));
            ny_fp = FP_DIV(INT_TO_FP(dy), INT_TO_FP(render_r));
            nz_fp = FP_ONE - FP_MUL(nx_fp, nx_fp) - FP_MUL(ny_fp, ny_fp);
            if (nz_fp < 0) nz_fp = 0;
            nz_fp = (LONG)isqrt((UWORD)(nz_fp << 8));

            dot = FP_MUL(nx_fp, LIGHT_X) + FP_MUL(ny_fp, LIGHT_Y) + FP_MUL(nz_fp, LIGHT_Z);
            diffuse = dot + FP_HALF;
            if (diffuse < 32) diffuse = 32;
            if (diffuse > FP_ONE) diffuse = FP_ONE;

            u = (WORD)((((LONG)dx * 128) / render_r) + rot_offset) & 0xFF;
            v = (WORD)(((LONG)(dy + render_r) * CHECKER_DIVS) / (render_r * 2));
            checker = ((u * CHECKER_DIVS / 256) + v) & 1;

            shade = 6 - (WORD)((diffuse * 6) >> FP_SHIFT);
            if (shade < 0) shade = 0;
            if (shade > 6) shade = 6;

            {
                LONG spec = FP_MUL(nx_fp, HALF_X) + FP_MUL(ny_fp, HALF_Y) + FP_MUL(nz_fp, HALF_Z);

                if (checker) {
                    color = COL_RED_BASE + shade;
                } else {
                    color = COL_WHITE_BASE + shade;
                }

                if (spec > 190) {
                    WORD boost = (WORD)((spec - 190) * 6) / 55;
                    shade = shade - boost;
                    if (shade < 0) shade = 0;
                    if (checker) {
                        color = COL_RED_BASE + shade;
                    } else {
                        color = COL_WHITE_BASE + shade;
                    }
                }
                if (spec > 245) {
                    color = COL_SPECULAR;
                }
            }

            set_pixel(sf->bm, x, y, color);
        }
    }
}

void sphere_init(void)
{
    WORD d, i, p;
    WORD bpr = ((SPHERE_SIZE + 15) >> 4) << 1;

    for (d = 0; d < NUM_DEPTHS; d++) {
        for (i = 0; i < NUM_FRAMES; i++) {
            SphereFrame *sf = &sphere_frames[d][i];

            sf->bm = (struct BitMap *)AllocMem(sizeof(struct BitMap), MEMF_PUBLIC | MEMF_CLEAR);
            InitBitMap(sf->bm, SPHERE_DEPTH, SPHERE_SIZE, SPHERE_SIZE);
            for (p = 0; p < SPHERE_DEPTH; p++) {
                sf->bm->Planes[p] = AllocRaster(SPHERE_SIZE, SPHERE_SIZE);
                if (sf->bm->Planes[p])
                    BltClear(sf->bm->Planes[p], bpr * SPHERE_SIZE, 0);
            }

            sf->mask = (struct BitMap *)AllocMem(sizeof(struct BitMap), MEMF_PUBLIC | MEMF_CLEAR);
            InitBitMap(sf->mask, 1, SPHERE_SIZE, SPHERE_SIZE);
            sf->mask->Planes[0] = AllocRaster(SPHERE_SIZE, SPHERE_SIZE);
            if (sf->mask->Planes[0])
                BltClear(sf->mask->Planes[0], bpr * SPHERE_SIZE, 0);

            render_frame(d, i);
        }
    }
}

void sphere_cleanup(void)
{
    WORD d, i, p;

    for (d = 0; d < NUM_DEPTHS; d++) {
        for (i = 0; i < NUM_FRAMES; i++) {
            SphereFrame *sf = &sphere_frames[d][i];

            if (sf->bm) {
                for (p = 0; p < SPHERE_DEPTH; p++) {
                    if (sf->bm->Planes[p])
                        FreeRaster(sf->bm->Planes[p], SPHERE_SIZE, SPHERE_SIZE);
                }
                FreeMem(sf->bm, sizeof(struct BitMap));
                sf->bm = NULL;
            }
            if (sf->mask) {
                if (sf->mask->Planes[0])
                    FreeRaster(sf->mask->Planes[0], SPHERE_SIZE, SPHERE_SIZE);
                FreeMem(sf->mask, sizeof(struct BitMap));
                sf->mask = NULL;
            }
        }
    }
}
