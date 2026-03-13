#ifndef RENDER_H
#define RENDER_H

#include <exec/types.h>
#include <graphics/rastport.h>
#include "physics.h"
#include "sphere.h"

/* Floor/grid parameters */
#define FLOOR_COLOR     17    /* Brown/tan base */
#define GRID_COLOR      18    /* Darker grid lines */
#define GRID_HORIZON    180   /* Y line where floor starts */
#define SHADOW_COLOR    16    /* Dark shadow on floor */

/* Background palette slots */
#define SKY_COLOR       0
#define FLOOR_BASE      17
#define FLOOR_GRID      18
#define FLOOR_LIGHT     19

void render_init(void);
void render_frame(struct RastPort *rp, BallState *ball, BOOL show_credits);

#endif
