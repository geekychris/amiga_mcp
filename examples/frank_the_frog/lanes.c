/*
 * Frank the Frog - Lane objects (traffic + river)
 * Tuned difficulty: gentler speeds, better spacing.
 */

#include <exec/types.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <proto/graphics.h>

#include <string.h>

#include "game.h"
#include "lanes.h"

/* A single object in a lane (car, truck, log, turtle group) */
typedef struct {
    int x;          /* pixel x position (can be negative for wrap) */
    int width;      /* width in pixels */
} LaneObj;

/* A lane definition */
typedef struct {
    int row;        /* screen row */
    int dir;        /* DIR_LEFT or DIR_RIGHT */
    int speed;      /* pixels per tick (movement accumulates) */
    int tick_div;   /* move every N frames (for sub-pixel speed) */
    int tick_cnt;   /* frame counter */
    int num_objs;
    UBYTE color;    /* draw color */
    int is_river;   /* 1 = river (logs/turtles), 0 = road (cars/trucks) */
    LaneObj objs[MAX_LANE_OBJECTS];
} Lane;

/* 10 lanes: 5 road + 5 river */
#define NUM_LANES 10
static Lane lanes[NUM_LANES];

/* Home base slots */
static int home_filled[5] = {0, 0, 0, 0, 0};

/* Draw a filled frog icon in a home slot */
static void draw_home_frog(struct RastPort *rp, int cx)
{
    int x0 = cx - 5;
    int y0 = ROW_HOME * TILE_H + 3;
    SetAPen(rp, (long)COL_FROG);
    RectFill(rp, x0, y0, x0 + 10, y0 + 10);
    SetAPen(rp, (long)COL_WHITE);
    WritePixel(rp, x0 + 2, y0 + 2);
    WritePixel(rp, x0 + 8, y0 + 2);
}

static void setup_lane(int idx, int row, int dir, int speed, int tick_div,
                        UBYTE color, int is_river, int num, int obj_w, int spacing)
{
    int i;
    Lane *l = &lanes[idx];

    l->row = row;
    l->dir = dir;
    l->speed = speed;
    l->tick_div = tick_div;
    l->tick_cnt = 0;
    l->num_objs = num;
    l->color = color;
    l->is_river = is_river;

    for (i = 0; i < num && i < MAX_LANE_OBJECTS; i++) {
        l->objs[i].x = i * spacing;
        l->objs[i].width = obj_w;
    }
}

void lanes_init(int level)
{
    int base;

    memset(lanes, 0, sizeof(lanes));

    /* Speed scaling: level 1 feels like classic Frogger, higher levels get intense */
    base = level;
    if (base > 6) base = 6;

    /* Road lanes: speed = pixels per frame, tick_div = 1 (every frame)
     * Level 1: 2-3 px/frame = ~100-150 px/sec at 50fps
     * Higher levels: +0.5-1 px/frame per level
     */
    setup_lane(0, ROW_ROAD_1, DIR_RIGHT, 2 + base/2, 1, COL_CAR_RED,    0, 3, 16, 120);
    setup_lane(1, ROW_ROAD_2, DIR_LEFT,  3 + base/3, 1, COL_CAR_YELLOW, 0, 3, 16, 110);
    setup_lane(2, ROW_ROAD_3, DIR_RIGHT, 2 + base/2, 1, COL_CAR_BLUE,   0, 3, 16, 110);
    setup_lane(3, ROW_ROAD_4, DIR_LEFT,  2 + base/3, 1, COL_TRUCK,      0, 2, 32, 160);
    setup_lane(4, ROW_ROAD_5, DIR_RIGHT, 3 + base/2, 1, COL_CAR_RED,    0, 3, 16, 120);

    /* Clamp tick_div minimum to 1 */
    {
        int i;
        for (i = 0; i < NUM_LANES; i++) {
            if (lanes[i].tick_div < 1) lanes[i].tick_div = 1;
        }
    }

    /* River lanes: logs and turtles */
    setup_lane(5, ROW_RIVER_1, DIR_RIGHT, 2 + base/3, 1, COL_LOG,    1, 3, 48, 110);
    setup_lane(6, ROW_RIVER_2, DIR_LEFT,  1 + base/3, 1, COL_TURTLE, 1, 3, 32, 100);
    setup_lane(7, ROW_RIVER_3, DIR_RIGHT, 2 + base/2, 1, COL_LOG,    1, 2, 64, 170);
    setup_lane(8, ROW_RIVER_4, DIR_LEFT,  2 + base/3, 1, COL_TURTLE, 1, 3, 32, 110);
    setup_lane(9, ROW_RIVER_5, DIR_RIGHT, 2 + base/2, 1, COL_LOG,    1, 2, 48, 160);
}

void lanes_tick(void)
{
    int i, j;

    for (i = 0; i < NUM_LANES; i++) {
        Lane *l = &lanes[i];

        l->tick_cnt++;
        if (l->tick_cnt < l->tick_div) continue;
        l->tick_cnt = 0;

        for (j = 0; j < l->num_objs; j++) {
            l->objs[j].x += l->speed * l->dir;

            /* Wrap around */
            if (l->dir == DIR_RIGHT && l->objs[j].x > SCREEN_W + 16) {
                l->objs[j].x = -l->objs[j].width;
            } else if (l->dir == DIR_LEFT && l->objs[j].x + l->objs[j].width < -16) {
                l->objs[j].x = SCREEN_W;
            }
        }
    }
}

/* Draw a car shape */
static void draw_car(struct RastPort *rp, int x, int y, UBYTE color)
{
    /* Car body */
    SetAPen(rp, (long)color);
    RectFill(rp, x + 1, y + 3, x + 14, y + 12);
    /* Roof */
    RectFill(rp, x + 4, y + 1, x + 11, y + 3);
    /* Wheels */
    SetAPen(rp, (long)COL_BG);
    RectFill(rp, x + 2, y + 12, x + 4, y + 14);
    RectFill(rp, x + 11, y + 12, x + 13, y + 14);
    /* Windshield */
    SetAPen(rp, (long)COL_WATER);
    RectFill(rp, x + 5, y + 2, x + 10, y + 3);
}

/* Draw a truck shape - cab faces the direction of travel */
static void draw_truck(struct RastPort *rp, int x, int y, UBYTE color, int dir)
{
    if (dir == DIR_RIGHT) {
        /* Cab on right (front), cargo on left (back) */
        SetAPen(rp, (long)color);
        RectFill(rp, x + 1, y + 2, x + 22, y + 12);   /* Cargo */
        RectFill(rp, x + 23, y + 4, x + 30, y + 12);   /* Cab */
        SetAPen(rp, (long)COL_BG);
        RectFill(rp, x + 3, y + 12, x + 6, y + 14);    /* Rear wheel */
        RectFill(rp, x + 16, y + 12, x + 19, y + 14);  /* Mid wheel */
        RectFill(rp, x + 25, y + 12, x + 28, y + 14);  /* Front wheel */
        SetAPen(rp, (long)COL_WATER);
        RectFill(rp, x + 24, y + 5, x + 29, y + 7);    /* Windshield */
    } else {
        /* Cab on left (front), cargo on right (back) */
        SetAPen(rp, (long)color);
        RectFill(rp, x + 1, y + 4, x + 8, y + 12);     /* Cab */
        RectFill(rp, x + 9, y + 2, x + 30, y + 12);    /* Cargo */
        SetAPen(rp, (long)COL_BG);
        RectFill(rp, x + 3, y + 12, x + 6, y + 14);    /* Front wheel */
        RectFill(rp, x + 12, y + 12, x + 15, y + 14);  /* Mid wheel */
        RectFill(rp, x + 25, y + 12, x + 28, y + 14);  /* Rear wheel */
        SetAPen(rp, (long)COL_WATER);
        RectFill(rp, x + 2, y + 5, x + 7, y + 7);      /* Windshield */
    }
}

/* Draw a log */
static void draw_log(struct RastPort *rp, int x, int y, int w)
{
    /* Main log body */
    SetAPen(rp, (long)COL_LOG);
    RectFill(rp, x + 2, y + 3, x + w - 3, y + 12);
    /* Lighter top edge (bark highlight) */
    SetAPen(rp, (long)COL_CAR_YELLOW);
    Move(rp, x + 3, y + 3);
    Draw(rp, x + w - 4, y + 3);
    /* End caps (darker) */
    SetAPen(rp, (long)COL_FROG_DARK);
    RectFill(rp, x, y + 4, x + 2, y + 11);
    RectFill(rp, x + w - 3, y + 4, x + w - 1, y + 11);
    /* Knot details */
    SetAPen(rp, (long)COL_FROG_DARK);
    if (w > 40) {
        RectFill(rp, x + w/3, y + 6, x + w/3 + 2, y + 8);
        RectFill(rp, x + 2*w/3, y + 5, x + 2*w/3 + 2, y + 9);
    }
}

/* Draw a turtle group */
static void draw_turtles(struct RastPort *rp, int x, int y, int w)
{
    int i;
    int num = w / 12;
    if (num < 1) num = 1;

    for (i = 0; i < num; i++) {
        int tx = x + i * 12 + 1;
        /* Shell */
        SetAPen(rp, (long)COL_TURTLE);
        RectFill(rp, tx + 1, y + 3, tx + 9, y + 11);
        /* Shell highlight */
        SetAPen(rp, (long)COL_GRASS);
        RectFill(rp, tx + 3, y + 4, tx + 7, y + 6);
        /* Head */
        SetAPen(rp, (long)COL_TURTLE);
        RectFill(rp, tx + 4, y + 1, tx + 6, y + 3);
        /* Flippers */
        WritePixel(rp, tx, y + 6);
        WritePixel(rp, tx + 10, y + 6);
        /* Legs */
        WritePixel(rp, tx + 1, y + 12);
        WritePixel(rp, tx + 9, y + 12);
    }
}

void lanes_draw(struct RastPort *rp)
{
    int i, j;

    for (i = 0; i < NUM_LANES; i++) {
        Lane *l = &lanes[i];
        int y = l->row * TILE_H;

        for (j = 0; j < l->num_objs; j++) {
            int x = l->objs[j].x;
            int w = l->objs[j].width;

            /* Skip if any part is off screen - avoids negative coord blitter bugs */
            if (x < 0 || x + w > SCREEN_W) continue;

            if (l->is_river) {
                if (l->color == COL_TURTLE) {
                    draw_turtles(rp, x, y, w);
                } else {
                    draw_log(rp, x, y, w);
                }
            } else {
                if (w >= 32) {
                    draw_truck(rp, x, y, l->color, l->dir);
                } else {
                    draw_car(rp, x, y, l->color);
                }
            }
        }
    }

    /* Draw frogs in filled home slots */
    {
        int slot;
        for (slot = 0; slot < 5; slot++) {
            if (home_filled[slot]) {
                int cx = slot * (SCREEN_W / 5) + (SCREEN_W / 10);
                draw_home_frog(rp, cx);
            }
        }
    }
}

int lanes_check_car(int px, int py)
{
    int i, j;
    int frog_row_idx = py / TILE_H;

    for (i = 0; i < NUM_LANES; i++) {
        Lane *l = &lanes[i];
        if (l->is_river) continue;
        if (l->row != frog_row_idx) continue;

        for (j = 0; j < l->num_objs; j++) {
            int ox = l->objs[j].x;
            int ow = l->objs[j].width;

            /* AABB overlap with small inset for forgiving collisions */
            if (px + TILE_W - 5 > ox + 2 && px + 5 < ox + ow - 2) {
                return 1;
            }
        }
    }
    return 0;
}

int lanes_check_river(int px, int py, int *ride_dx)
{
    int i, j;
    int frog_row_idx = py / TILE_H;

    *ride_dx = 0;

    /* Only check river rows */
    if (frog_row_idx < ROW_RIVER_5 || frog_row_idx > ROW_RIVER_1) {
        return 1; /* not in river, safe */
    }

    for (i = 0; i < NUM_LANES; i++) {
        Lane *l = &lanes[i];
        if (!l->is_river) continue;
        if (l->row != frog_row_idx) continue;

        for (j = 0; j < l->num_objs; j++) {
            int ox = l->objs[j].x;
            int ow = l->objs[j].width;

            if (px + TILE_W - 4 > ox && px + 4 < ox + ow) {
                /* Riding speed: only applies on movement frames */
                if (l->tick_cnt == 0) {
                    *ride_dx = l->speed * l->dir;
                }
                return 1; /* on a log/turtle */
            }
        }
    }

    return 0; /* in water, not on anything = death */
}

int lanes_check_home(int px)
{
    int slot;

    for (slot = 0; slot < 5; slot++) {
        int cx = slot * (SCREEN_W / 5) + (SCREEN_W / 10);
        int pad_w = 32;  /* generous landing zone */

        if (px + TILE_W / 2 >= cx - pad_w / 2 &&
            px + TILE_W / 2 <= cx + pad_w / 2) {
            if (home_filled[slot]) return -1; /* already filled */
            return slot;
        }
    }
    return -1; /* missed the slot */
}

void lanes_fill_home(int slot)
{
    if (slot >= 0 && slot < 5) {
        home_filled[slot] = 1;
    }
}

int lanes_all_home(void)
{
    int i;
    for (i = 0; i < 5; i++) {
        if (!home_filled[i]) return 0;
    }
    return 1;
}

void lanes_reset_home(void)
{
    int i;
    for (i = 0; i < 5; i++) {
        home_filled[i] = 0;
    }
}
