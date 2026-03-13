#include "render.h"
#include "tables.h"

#include <proto/graphics.h>

/* Grid perspective */
#define VANISH_X  160
#define VANISH_Y  170
#define NUM_HLINES  8
#define NUM_VLINES 12

void render_init(void)
{
}

static void draw_floor(struct RastPort *rp)
{
    WORD i;
    WORD y_start = GRID_HORIZON;
    WORD y_end = SCREEN_H - 1;

    SetAPen(rp, FLOOR_COLOR);
    RectFill(rp, 0, y_start, SCREEN_W - 1, y_end);

    SetAPen(rp, GRID_COLOR);
    for (i = 0; i < NUM_HLINES; i++) {
        LONG t = ((LONG)(i + 1) * (i + 1) * (y_end - y_start)) / ((LONG)(NUM_HLINES + 1) * (NUM_HLINES + 1));
        WORD y = y_start + (WORD)t;
        if (y <= y_end) {
            Move(rp, 0, y);
            Draw(rp, SCREEN_W - 1, y);
        }
    }

    for (i = 0; i < NUM_VLINES; i++) {
        WORD bottom_x = (i * SCREEN_W) / (NUM_VLINES - 1);
        WORD top_x = VANISH_X + ((bottom_x - VANISH_X) * 2) / 5;
        Move(rp, top_x, y_start);
        Draw(rp, bottom_x, y_end);
    }
}

static void draw_shadow(struct RastPort *rp, BallState *ball)
{
    WORD ball_y = FP_TO_INT(ball->y);
    WORD ball_x = FP_TO_INT(ball->x);
    WORD height_above = FLOOR_Y - ball_y;
    WORD shadow_w, shadow_h;
    WORD sx, sy, i;
    /* Depth scale: near (depth 0) = bigger shadow, far = smaller */
    WORD depth_scale = 4 - ball->depth;  /* 4, 3, 2 */

    if (height_above < 0) height_above = 0;

    shadow_w = (BALL_RADIUS * depth_scale) / 3 + (height_above / 4);
    shadow_h = (3 * depth_scale) / 2 + (height_above / 16);
    if (shadow_h > 14) shadow_h = 14;

    sx = ball_x + (height_above / 6);
    sy = FLOOR_Y + 2;

    SetAPen(rp, SHADOW_COLOR);
    for (i = -shadow_h; i <= shadow_h; i++) {
        WORD half_w = (shadow_w * isqrt((UWORD)((shadow_h * shadow_h - i * i) << 8))) >> 8;
        if (half_w <= 0) half_w = 1;
        if (sy + i >= GRID_HORIZON && sy + i < SCREEN_H) {
            Move(rp, sx - half_w, sy + i);
            Draw(rp, sx + half_w, sy + i);
        }
    }
}

static void draw_ball(struct RastPort *rp, BallState *ball)
{
    WORD frame = (ball->rot_angle * NUM_FRAMES) / TABLE_SIZE;
    WORD depth = ball->depth;
    WORD bx = FP_TO_INT(ball->x) - SPHERE_RADIUS;
    WORD by = FP_TO_INT(ball->y) - SPHERE_RADIUS;
    SphereFrame *sf;

    if (frame < 0) frame = 0;
    if (frame >= NUM_FRAMES) frame = NUM_FRAMES - 1;
    if (depth < 0) depth = 0;
    if (depth >= NUM_DEPTHS) depth = NUM_DEPTHS - 1;

    sf = &sphere_frames[depth][frame];

    BltMaskBitMapRastPort(
        sf->bm, 0, 0,
        rp, bx, by,
        SPHERE_SIZE, SPHERE_SIZE,
        (ABC | ABNC | ANBC),
        sf->mask->Planes[0]
    );
}

static void draw_credits(struct RastPort *rp)
{
    /* Credits overlay */
    SetAPen(rp, 15);  /* White */
    SetBPen(rp, 0);
    SetDrMd(rp, JAM2);

    Move(rp, 80, 100);
    Text(rp, "BOING BALL REIMAGINED", 21);

    Move(rp, 60, 120);
    Text(rp, "Original concept: Sam Dicker", 28);

    Move(rp, 60, 132);
    Text(rp, "Original demo:    Dale Luck", 27);

    Move(rp, 60, 152);
    Text(rp, "For the Amiga team who created", 30);

    Move(rp, 60, 164);
    Text(rp, "magic that inspired a generation", 31);

    Move(rp, 60, 184);
    Text(rp, "[SPACE] dismiss  [ESC] quit", 27);
    Move(rp, 60, 196);
    Text(rp, "[C] rainbow [F]ast [S]low", 25);

    SetDrMd(rp, JAM1);
}

void render_frame(struct RastPort *rp, BallState *ball, BOOL show_credits)
{
    /* Clear entire screen - sky area is color 0, copper sets actual gradient */
    SetRast(rp, 0);

    /* Draw floor */
    draw_floor(rp);

    /* Draw shadow on floor */
    draw_shadow(rp, ball);

    /* Draw ball */
    draw_ball(rp, ball);

    /* Credits overlay */
    if (show_credits) {
        draw_credits(rp);
    }
}
