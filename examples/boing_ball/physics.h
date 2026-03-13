#ifndef PHYSICS_H
#define PHYSICS_H

#include <exec/types.h>
#include "tables.h"

/* Screen bounds */
#define SCREEN_W 320
#define SCREEN_H 256

/* Ball dimensions */
#define BALL_DIAMETER 96
#define BALL_RADIUS  48

/* Play area (pixel coords) */
#define FLOOR_Y    210
#define CEILING_Y   44
#define LEFT_WALL   10
#define RIGHT_WALL (SCREEN_W - 10)

/* Physics in 8.8 fixed point */
#define GRAVITY      48    /* ~0.19 px/frame^2 */
#define BOUNCE_DAMP 256    /* floor/ceiling: perfectly elastic (1.0) */
#define WALL_DAMP   256    /* wall: perfectly elastic (1.0) */
#define X_SPEED     512    /* horizontal speed (~2 px/frame) */
#define INIT_VY    (-900)  /* initial upward velocity (~-3.5 px/frame) */

typedef struct {
    LONG x, y;       /* Position in 8.8 fixed point (center of ball) */
    LONG vx, vy;     /* Velocity in 8.8 fixed point */
    WORD rot_angle;   /* Rotation angle index (0..255) */
    WORD rot_speed;   /* Rotation speed (indices per frame) */
    WORD squash;      /* Squash/stretch factor 8.8, FP_ONE = normal */
    WORD squash_timer;/* Frames remaining for squash effect */
    WORD z_phase;     /* Z-axis oscillation phase (0..255) */
    WORD z_speed;     /* Z oscillation speed */
    WORD depth;       /* Current depth index (0=near, NUM_DEPTHS-1=far) */
    BOOL bounced;     /* Set on frame of bounce (for sound trigger) */
} BallState;

void physics_init(BallState *ball);
void physics_update(BallState *ball);

#endif
