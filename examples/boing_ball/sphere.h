#ifndef SPHERE_H
#define SPHERE_H

#include <exec/types.h>
#include <graphics/gfx.h>

/* Sphere rendering parameters */
#define SPHERE_SIZE     96      /* Bitmap dimensions (all depths same size) */
#define SPHERE_RADIUS   48      /* Half of SPHERE_SIZE, used for blit centering */
#define NUM_FRAMES       8      /* Rotation frames per depth */
#define NUM_DEPTHS       8      /* Depth levels: near (large) to far (small) */
#define CHECKER_DIVS     8      /* Checkerboard divisions */
#define SPHERE_DEPTH     5      /* Bitplanes (32 colors) */

/* Each frame is a pre-rendered bitmap + mask */
typedef struct {
    struct BitMap *bm;
    struct BitMap *mask;
} SphereFrame;

extern SphereFrame sphere_frames[NUM_DEPTHS][NUM_FRAMES];

void sphere_init(void);
void sphere_cleanup(void);

#endif
