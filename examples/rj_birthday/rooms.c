/*
 * rooms.c - Room background rendering
 * Each room is 320px wide, drawn relative to camera position
 */
#include <proto/graphics.h>
#include <graphics/rastport.h>
#include <string.h>
#include "game.h"

/* Room name strings */
static const char *room_names[ROOM_COUNT] = {
    "THE FOYER",
    "AMSTERDAM LOUNGE",
    "TOKYO ROOM",
    "SILICON VALLEY DEN",
    "BAY AREA PATIO",
    "THE LIVING ROOM"
};

const char *room_name(WORD room)
{
    if (room >= 0 && room < ROOM_COUNT)
        return room_names[room];
    return "";
}

/* Clip-aware rectangle fill: sx is screen X offset of the room */
static void room_rect(struct RastPort *rp, WORD sx,
                       WORD x1, WORD y1, WORD x2, WORD y2, WORD color)
{
    WORD rx1 = x1 + sx;
    WORD rx2 = x2 + sx;

    /* Clip to screen */
    if (rx2 < 0 || rx1 >= SCREEN_W) return;
    if (rx1 < 0) rx1 = 0;
    if (rx2 >= SCREEN_W) rx2 = SCREEN_W - 1;
    if (y1 < HUD_H) y1 = HUD_H;
    if (y2 >= SCREEN_H) y2 = SCREEN_H - 1;
    if (y1 > y2) return;

    SetAPen(rp, color);
    RectFill(rp, rx1, y1, rx2, y2);
}

/* Clip-aware line */
static void room_line(struct RastPort *rp, WORD sx,
                       WORD x1, WORD y1, WORD x2, WORD y2, WORD color)
{
    WORD rx1 = x1 + sx;
    WORD rx2 = x2 + sx;

    if (rx1 < 0 && rx2 < 0) return;
    if (rx1 >= SCREEN_W && rx2 >= SCREEN_W) return;

    SetAPen(rp, color);
    Move(rp, rx1, y1);
    Draw(rp, rx2, y2);
}

/* Clip-aware text */
static void room_text(struct RastPort *rp, WORD sx, WORD x, WORD y,
                        const char *str, WORD color)
{
    WORD rx = x + sx;
    if (rx >= SCREEN_W || rx + (WORD)strlen(str) * 8 < 0) return;

    SetAPen(rp, color);
    Move(rp, rx, y);
    Text(rp, (CONST_STRPTR)str, strlen(str));
}

/* --- Individual room renderers --- */

static void draw_foyer(struct RastPort *rp, WORD sx)
{
    /* Floor */
    room_rect(rp, sx, 0, FLOOR_Y, 319, SCREEN_H - 1, COL_DKRED);
    /* Back wall */
    room_rect(rp, sx, 0, HUD_H, 319, FLOOR_Y - 1, COL_TAN);
    /* Wainscoting */
    room_rect(rp, sx, 0, FLOOR_Y - 20, 319, FLOOR_Y - 1, COL_BROWN);
    /* Door frame on right */
    room_rect(rp, sx, 290, HUD_H + 10, 310, FLOOR_Y - 1, COL_DKBROWN);
    room_rect(rp, sx, 295, HUD_H + 15, 305, FLOOR_Y - 5, COL_LTBLUE);
    /* Welcome mat */
    room_rect(rp, sx, 270, FLOOR_Y + 2, 320, FLOOR_Y + 10, COL_GREEN);
    /* Gift table on left */
    room_rect(rp, sx, 10, FLOOR_Y - 35, 60, FLOOR_Y - 25, COL_BROWN);
    room_rect(rp, sx, 15, FLOOR_Y - 34, 20, FLOOR_Y - 1, COL_DKBROWN);
    room_rect(rp, sx, 50, FLOOR_Y - 34, 55, FLOOR_Y - 1, COL_DKBROWN);
    /* Banner */
    room_text(rp, sx, 80, HUD_H + 30, "HAPPY 70TH!", COL_YELLOW);
    /* Balloons (simple circles) */
    room_rect(rp, sx, 40, HUD_H + 8, 48, HUD_H + 18, COL_RED);
    room_rect(rp, sx, 140, HUD_H + 5, 148, HUD_H + 15, COL_BLUE);
    room_rect(rp, sx, 240, HUD_H + 8, 248, HUD_H + 18, COL_YELLOW);
    /* Balloon strings */
    room_line(rp, sx, 44, HUD_H + 18, 44, HUD_H + 35, COL_WHITE);
    room_line(rp, sx, 144, HUD_H + 15, 144, HUD_H + 32, COL_WHITE);
    room_line(rp, sx, 244, HUD_H + 18, 244, HUD_H + 35, COL_WHITE);
}

static void draw_amsterdam(struct RastPort *rp, WORD sx)
{
    WORD lane;
    /* Floor - dark wood */
    room_rect(rp, sx, 0, FLOOR_Y, 319, SCREEN_H - 1, COL_DKBROWN);
    /* Walls - hazy green tint */
    room_rect(rp, sx, 0, HUD_H, 319, FLOOR_Y - 1, COL_GREEN);
    /* Haze effect - lighter patches */
    room_rect(rp, sx, 50, HUD_H + 20, 100, HUD_H + 50, COL_LTGREEN);
    room_rect(rp, sx, 180, HUD_H + 30, 230, HUD_H + 55, COL_LTGREEN);
    /* Bar counter - 3 lanes */
    for (lane = 0; lane < TAPPER_LANES; lane++) {
        WORD ly = TAPPER_LANE_Y0 + lane * TAPPER_LANE_H;
        room_rect(rp, sx, 10, ly, 310, ly + 3, COL_BROWN);    /* counter top */
        room_rect(rp, sx, 10, ly + 4, 310, ly + 6, COL_DKBROWN); /* counter front */
    }
    /* Bar back (shelves) */
    room_rect(rp, sx, 0, HUD_H + 60, 20, FLOOR_Y - 1, COL_BROWN);
    room_rect(rp, sx, 3, HUD_H + 65, 17, HUD_H + 75, COL_ORANGE);
    room_rect(rp, sx, 3, HUD_H + 80, 17, HUD_H + 90, COL_ORANGE);
    /* Sign */
    room_text(rp, sx, 100, HUD_H + 25, "COFFEE SHOP", COL_BTYELLOW);
    /* Tulip decoration */
    room_rect(rp, sx, 280, HUD_H + 50, 285, HUD_H + 60, COL_RED);
    room_rect(rp, sx, 282, HUD_H + 60, 283, HUD_H + 75, COL_GREEN);
}

static void draw_tokyo(struct RastPort *rp, WORD sx)
{
    WORD lane;
    /* Floor - tatami */
    room_rect(rp, sx, 0, FLOOR_Y, 319, SCREEN_H - 1, COL_TAN);
    /* Walls - paper screen */
    room_rect(rp, sx, 0, HUD_H, 319, FLOOR_Y - 1, COL_WHITE);
    /* Shoji screen grid */
    room_line(rp, sx, 80, HUD_H, 80, FLOOR_Y - 1, COL_TAN);
    room_line(rp, sx, 160, HUD_H, 160, FLOOR_Y - 1, COL_TAN);
    room_line(rp, sx, 240, HUD_H, 240, FLOOR_Y - 1, COL_TAN);
    room_line(rp, sx, 0, HUD_H + 60, 319, HUD_H + 60, COL_TAN);
    room_line(rp, sx, 0, HUD_H + 120, 319, HUD_H + 120, COL_TAN);
    /* Conveyor belt lanes */
    for (lane = 0; lane < SUSHI_LANES; lane++) {
        WORD ly = SUSHI_LANE_Y0 + lane * SUSHI_LANE_H;
        room_rect(rp, sx, 10, ly, 310, ly + 2, COL_GREY);    /* belt */
        room_rect(rp, sx, 10, ly + 14, 310, ly + 16, COL_GREY); /* belt edge */
    }
    /* Lantern */
    room_rect(rp, sx, 150, HUD_H + 10, 170, HUD_H + 30, COL_RED);
    room_line(rp, sx, 160, HUD_H, 160, HUD_H + 10, COL_DKBROWN);
    /* Sign */
    room_text(rp, sx, 90, HUD_H + 45, "SUSHI BAR", COL_RED);
}

static void draw_silicon(struct RastPort *rp, WORD sx)
{
    /* Floor - grey carpet */
    room_rect(rp, sx, 0, FLOOR_Y, 319, SCREEN_H - 1, COL_GREY);
    /* Walls - blue-ish */
    room_rect(rp, sx, 0, HUD_H, 319, FLOOR_Y - 1, COL_BLUE);
    /* Arcade cabinets */
    /* Sinistar */
    room_rect(rp, sx, 20, FLOOR_Y - 60, 55, FLOOR_Y - 1, COL_DKBROWN);
    room_rect(rp, sx, 25, FLOOR_Y - 55, 50, FLOOR_Y - 25, COL_BG);
    room_text(rp, sx, 26, FLOOR_Y - 40, "SIN", COL_RED);
    /* Red Baron */
    room_rect(rp, sx, 65, FLOOR_Y - 60, 100, FLOOR_Y - 1, COL_DKBROWN);
    room_rect(rp, sx, 70, FLOOR_Y - 55, 95, FLOOR_Y - 25, COL_BG);
    room_text(rp, sx, 72, FLOOR_Y - 40, "RB", COL_ORANGE);
    /* Display cases: Amiga, Lynx, 3DO */
    /* Amiga */
    room_rect(rp, sx, 140, FLOOR_Y - 45, 175, FLOOR_Y - 20, COL_TAN);
    room_rect(rp, sx, 145, FLOOR_Y - 43, 170, FLOOR_Y - 22, COL_WHITE);
    room_text(rp, sx, 143, FLOOR_Y - 30, "A1000", COL_BLUE);
    /* Lynx */
    room_rect(rp, sx, 200, FLOOR_Y - 45, 235, FLOOR_Y - 20, COL_TAN);
    room_rect(rp, sx, 205, FLOOR_Y - 43, 230, FLOOR_Y - 22, COL_GREY);
    room_text(rp, sx, 205, FLOOR_Y - 30, "LYNX", COL_RED);
    /* 3DO */
    room_rect(rp, sx, 260, FLOOR_Y - 45, 295, FLOOR_Y - 20, COL_TAN);
    room_rect(rp, sx, 265, FLOOR_Y - 43, 290, FLOOR_Y - 22, COL_BG);
    room_text(rp, sx, 268, FLOOR_Y - 30, "3DO", COL_BTYELLOW);
    /* Tech posters on the wall */
    room_rect(rp, sx, 130, HUD_H + 10, 190, HUD_H + 30, COL_BTYELLOW);
    room_text(rp, sx, 135, HUD_H + 24, "AMIGA", COL_BLUE);
    room_rect(rp, sx, 230, HUD_H + 10, 290, HUD_H + 30, COL_LTBLUE);
    room_text(rp, sx, 240, HUD_H + 24, "1985", COL_WHITE);
}

static void draw_bayarea(struct RastPort *rp, WORD sx)
{
    WORD i;
    /* Ground / patio */
    room_rect(rp, sx, 0, FLOOR_Y, 319, SCREEN_H - 1, COL_TAN);
    /* Sky */
    room_rect(rp, sx, 0, HUD_H, 319, FLOOR_Y - 1, COL_LTBLUE);
    /* Golden Gate Bridge silhouette */
    room_rect(rp, sx, 30, HUD_H + 20, 290, HUD_H + 25, COL_RED); /* deck */
    room_rect(rp, sx, 60, HUD_H + 5, 65, HUD_H + 25, COL_RED);   /* tower1 */
    room_rect(rp, sx, 230, HUD_H + 5, 235, HUD_H + 25, COL_RED);  /* tower2 */
    /* Cables */
    for (i = 0; i < 10; i++) {
        WORD cx = 60 + i * 19;
        room_line(rp, sx, 62, HUD_H + 5, cx, HUD_H + 18, COL_ORANGE);
    }
    /* Water */
    room_rect(rp, sx, 0, HUD_H + 30, 319, HUD_H + 50, COL_BLUE);
    /* Patio tables */
    room_rect(rp, sx, 50, FLOOR_Y - 15, 80, FLOOR_Y - 5, COL_BROWN);
    room_rect(rp, sx, 150, FLOOR_Y - 15, 180, FLOOR_Y - 5, COL_BROWN);
    room_rect(rp, sx, 250, FLOOR_Y - 15, 280, FLOOR_Y - 5, COL_BROWN);
    /* SF sign */
    room_text(rp, sx, 120, HUD_H + 60, "SAN FRANCISCO", COL_YELLOW);
    /* Scooter lane marker */
    room_line(rp, sx, 0, FLOOR_Y - 25, 319, FLOOR_Y - 25, COL_BTYELLOW);
}

static void draw_living(struct RastPort *rp, WORD sx)
{
    /* Floor - dance floor pattern */
    room_rect(rp, sx, 0, FLOOR_Y, 319, SCREEN_H - 1, COL_DKRED);
    /* Checkerboard dance floor */
    {
        WORD cx, cy;
        for (cy = FLOOR_Y; cy < SCREEN_H; cy += 16) {
            for (cx = 0; cx < 320; cx += 16) {
                if (((cx / 16) + (cy / 16)) & 1)
                    room_rect(rp, sx, cx, cy, cx + 15, cy + 15, COL_GREY);
            }
        }
    }
    /* Walls */
    room_rect(rp, sx, 0, HUD_H, 319, FLOOR_Y - 1, COL_PINK);
    /* Disco ball */
    room_rect(rp, sx, 152, HUD_H + 5, 168, HUD_H + 21, COL_WHITE);
    room_rect(rp, sx, 155, HUD_H + 8, 165, HUD_H + 18, COL_GREY);
    room_line(rp, sx, 160, HUD_H, 160, HUD_H + 5, COL_GREY);
    /* Birthday cake (center) */
    room_rect(rp, sx, 140, FLOOR_Y - 30, 180, FLOOR_Y - 1, COL_WHITE);  /* cake */
    room_rect(rp, sx, 142, FLOOR_Y - 32, 178, FLOOR_Y - 30, COL_PINK);  /* frosting top */
    room_rect(rp, sx, 142, FLOOR_Y - 20, 178, FLOOR_Y - 18, COL_PINK);  /* frosting mid */
    /* Candles */
    room_rect(rp, sx, 150, FLOOR_Y - 38, 152, FLOOR_Y - 32, COL_BTYELLOW);
    room_rect(rp, sx, 160, FLOOR_Y - 38, 162, FLOOR_Y - 32, COL_BTYELLOW);
    room_rect(rp, sx, 170, FLOOR_Y - 38, 172, FLOOR_Y - 32, COL_BTYELLOW);
    /* Candle flames */
    room_rect(rp, sx, 150, FLOOR_Y - 41, 152, FLOOR_Y - 38, COL_ORANGE);
    room_rect(rp, sx, 160, FLOOR_Y - 41, 162, FLOOR_Y - 38, COL_ORANGE);
    room_rect(rp, sx, 170, FLOOR_Y - 41, 172, FLOOR_Y - 38, COL_ORANGE);
    /* "70" on cake */
    room_text(rp, sx, 153, FLOOR_Y - 10, "70", COL_RED);
    /* Streamers */
    room_line(rp, sx, 20, HUD_H + 10, 80, HUD_H + 25, COL_YELLOW);
    room_line(rp, sx, 240, HUD_H + 10, 300, HUD_H + 25, COL_YELLOW);
    room_line(rp, sx, 50, HUD_H + 15, 120, HUD_H + 5, COL_RED);
    room_line(rp, sx, 200, HUD_H + 15, 270, HUD_H + 5, COL_RED);
}

/* Room draw functions table */
typedef void (*RoomDrawFunc)(struct RastPort *, WORD);
static RoomDrawFunc room_drawers[ROOM_COUNT] = {
    draw_foyer,
    draw_amsterdam,
    draw_tokyo,
    draw_silicon,
    draw_bayarea,
    draw_living
};

void rooms_draw_bg(struct RastPort *rp, GameState *gs)
{
    WORD cam = gs->camera_x;
    WORD left_room = cam / ROOM_W;
    WORD right_room = (cam + SCREEN_W - 1) / ROOM_W;
    WORD r;

    if (left_room < 0) left_room = 0;
    if (right_room >= ROOM_COUNT) right_room = ROOM_COUNT - 1;

    for (r = left_room; r <= right_room; r++) {
        WORD sx = r * ROOM_W - cam;  /* screen X offset of this room */
        room_drawers[r](rp, sx);
    }
}

void rooms_draw_details(struct RastPort *rp, GameState *gs)
{
    /* Room-specific animated/dynamic details could go here */
    /* For now, room backgrounds are fully drawn in rooms_draw_bg */
    (void)rp;
    (void)gs;
}
