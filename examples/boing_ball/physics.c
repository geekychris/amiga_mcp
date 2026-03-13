#include "physics.h"
#include "sphere.h"

void physics_init(BallState *ball)
{
    ball->x = INT_TO_FP(SCREEN_W / 2);
    ball->y = INT_TO_FP(CEILING_Y + 30);
    ball->vx = X_SPEED;
    ball->vy = INIT_VY;
    ball->rot_angle = 0;
    ball->rot_speed = 8;
    ball->squash = FP_ONE;
    ball->squash_timer = 0;
    ball->z_phase = 0;
    ball->z_speed = 3;
    ball->depth = 0;
    ball->bounced = FALSE;
}

void physics_update(BallState *ball)
{
    LONG bx, by;
    WORD z_val;

    ball->bounced = FALSE;

    /* Gravity */
    ball->vy += GRAVITY;

    /* Move */
    ball->x += ball->vx;
    ball->y += ball->vy;

    /* Floor bounce - only when moving downward */
    by = FP_TO_INT(ball->y) + BALL_RADIUS;
    if (by >= FLOOR_Y && ball->vy > 0) {
        ball->y = INT_TO_FP(FLOOR_Y - BALL_RADIUS);
        ball->vy = -ball->vy;
        ball->bounced = TRUE;
        ball->squash = INT_TO_FP(1) - FP_HALF / 3;
        ball->squash_timer = 6;
    }

    /* Ceiling bounce - only when moving upward */
    by = FP_TO_INT(ball->y) - BALL_RADIUS;
    if (by <= CEILING_Y && ball->vy < 0) {
        ball->y = INT_TO_FP(CEILING_Y + BALL_RADIUS);
        ball->vy = -ball->vy;
        ball->bounced = TRUE;
    }

    /* Wall bounces - reverse rotation direction */
    bx = FP_TO_INT(ball->x) - BALL_RADIUS;
    if (bx <= LEFT_WALL && ball->vx < 0) {
        ball->x = INT_TO_FP(LEFT_WALL + BALL_RADIUS);
        ball->vx = -ball->vx;
        ball->rot_speed = -ball->rot_speed;
        ball->bounced = TRUE;
    }
    bx = FP_TO_INT(ball->x) + BALL_RADIUS;
    if (bx >= RIGHT_WALL && ball->vx > 0) {
        ball->x = INT_TO_FP(RIGHT_WALL - BALL_RADIUS);
        ball->vx = -ball->vx;
        ball->rot_speed = -ball->rot_speed;
        ball->bounced = TRUE;
    }

    /* Squash/stretch decay */
    if (ball->squash_timer > 0) {
        ball->squash_timer--;
        if (ball->squash_timer == 0)
            ball->squash = FP_ONE;
    }

    /* Rotation */
    ball->rot_angle = (ball->rot_angle + ball->rot_speed) & (TABLE_SIZE - 1);

    /* Z-axis oscillation: sinusoidal depth bounce */
    ball->z_phase = (ball->z_phase + ball->z_speed) & (TABLE_SIZE - 1);
    /* sin_table ranges ~-240 to +240; map to 0..NUM_DEPTHS-1 */
    z_val = sin_table[ball->z_phase] + 240;  /* 0..480 */
    ball->depth = (z_val * NUM_DEPTHS) / 481;
    if (ball->depth >= NUM_DEPTHS) ball->depth = NUM_DEPTHS - 1;
    if (ball->depth < 0) ball->depth = 0;
}
