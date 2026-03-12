/*
 * Asteroids - Drawing routines using Amiga graphics.library
 */
#include <proto/graphics.h>
#include <graphics/gfxbase.h>
#include <string.h>
#include "draw.h"

/* Ship shape: triangle pointing right (angle 0), 5 vertices (closed) */
/* Coordinates relative to center, will be rotated */
static const WORD ship_x[] = { 10, -7, -4,  -4, -7 };
static const WORD ship_y[] = {  0, -6, -2,   2,  6 };
#define SHIP_VERTS 5

/* Thrust flame shape */
static const WORD flame_x[] = { -5, -12, -5 };
static const WORD flame_y[] = { -3,   0,  3 };
#define FLAME_VERTS 3

/* Rock shapes (4 variants per size) - offsets from center, 8 vertices each */
/* Large rocks (radius ~20) */
static const WORD rock_large[4][8][2] = {
    {{ -8,-18},{  8,-20},{ 20,-8},{ 18,  4},{ 12, 18},{ -4, 20},{-18, 10},{-20, -6}},
    {{-12,-16},{  4,-20},{ 18,-12},{ 20, 2},{ 10, 18},{ -8, 16},{-20,  4},{-16,-10}},
    {{ -6,-20},{ 10,-16},{ 20, -4},{ 16, 10},{  4, 20},{-10, 16},{-20,  2},{-14,-12}},
    {{-14,-14},{  2,-20},{ 16,-14},{ 20,  0},{ 14, 16},{ -2, 20},{-18,  8},{-18, -8}},
};

/* Medium rocks (radius ~12) */
static const WORD rock_medium[4][8][2] = {
    {{ -4,-10},{  4,-12},{ 12, -4},{ 10,  4},{  6, 10},{ -2, 12},{-10,  6},{-12, -2}},
    {{ -6, -8},{  2,-12},{ 10, -6},{ 12,  2},{  6, 10},{ -4, 10},{-12,  2},{ -8, -6}},
    {{ -2,-12},{  6,-10},{ 12, -2},{  8,  6},{  2, 12},{ -6,  8},{-12,  0},{ -8, -8}},
    {{ -8, -8},{  2,-12},{ 10, -8},{ 12,  0},{  8,  8},{  0, 12},{-10,  4},{-10, -4}},
};

/* Small rocks (radius ~6) */
static const WORD rock_small[4][8][2] = {
    {{ -2,-5},{ 2,-6},{ 6,-2},{ 5, 2},{ 3, 5},{-1, 6},{-5, 3},{-6,-1}},
    {{ -3,-4},{ 1,-6},{ 5,-3},{ 6, 1},{ 3, 5},{-2, 5},{-6, 1},{-4,-3}},
    {{ -1,-6},{ 3,-5},{ 6,-1},{ 4, 3},{ 1, 6},{-3, 4},{-6, 0},{-4,-4}},
    {{ -4,-4},{ 1,-6},{ 5,-4},{ 6, 0},{ 4, 4},{ 0, 6},{-5, 2},{-5,-2}},
};
#define ROCK_VERTS 8

/* Rotate point by angle (0..255) */
static void rotate(WORD px, WORD py, WORD angle, WORD *ox, WORD *oy)
{
    Fixed s = sin_tab[angle & 255];
    Fixed c = cos_tab[angle & 255];
    *ox = (WORD)((px * c - py * s) >> 16);
    *oy = (WORD)((px * s + py * c) >> 16);
}

void draw_clear(struct RastPort *rp)
{
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);
}

void draw_ship(struct RastPort *rp, Ship *ship, WORD frame)
{
    WORD sx, sy, rx, ry, i;

    if (!ship->alive) return;

    /* Blink during invulnerability */
    if (ship->invuln_timer > 0 && (frame & 4))
        return;

    sx = FIX_INT(ship->x);
    sy = FIX_INT(ship->y);

    /* Draw ship outline */
    SetAPen(rp, 1); /* white */
    {
        WORD fx, fy;
        rotate(ship_x[0], ship_y[0], ship->angle, &rx, &ry);
        fx = rx; fy = ry;
        Move(rp, sx + rx, sy + ry);
        for (i = 1; i < SHIP_VERTS; i++) {
            rotate(ship_x[i], ship_y[i], ship->angle, &rx, &ry);
            Draw(rp, sx + rx, sy + ry);
        }
        Draw(rp, sx + fx, sy + fy); /* close */
    }

    /* Draw thrust flame */
    if (ship->thrust_on && (frame & 2)) {
        SetAPen(rp, 9); /* orange */
        rotate(flame_x[0], flame_y[0], ship->angle, &rx, &ry);
        Move(rp, sx + rx, sy + ry);
        for (i = 1; i < FLAME_VERTS; i++) {
            rotate(flame_x[i], flame_y[i], ship->angle, &rx, &ry);
            Draw(rp, sx + rx, sy + ry);
        }
        rotate(flame_x[0], flame_y[0], ship->angle, &rx, &ry);
        Draw(rp, sx + rx, sy + ry);
    }
}

void draw_rocks(struct RastPort *rp, Rock *rocks)
{
    WORD i, j;
    for (i = 0; i < MAX_ROCKS; i++) {
        Rock *r = &rocks[i];
        WORD cx, cy;
        const WORD (*shape)[2];

        if (!r->active) continue;

        cx = FIX_INT(r->x);
        cy = FIX_INT(r->y);

        /* Choose shape table */
        switch (r->size) {
            case ROCK_LARGE:  shape = rock_large[r->shape & 3]; break;
            case ROCK_MEDIUM: shape = rock_medium[r->shape & 3]; break;
            default:          shape = rock_small[r->shape & 3]; break;
        }

        /* Color by size */
        switch (r->size) {
            case ROCK_LARGE:  SetAPen(rp, 7); break;  /* grey */
            case ROCK_MEDIUM: SetAPen(rp, 6); break;  /* darker grey */
            default:          SetAPen(rp, 5); break;  /* dim grey */
        }

        /* Draw polygon (no rotation — rocks are fixed shape) */
        Move(rp, cx + shape[0][0], cy + shape[0][1]);
        for (j = 1; j < ROCK_VERTS; j++) {
            Draw(rp, cx + shape[j][0], cy + shape[j][1]);
        }
        Draw(rp, cx + shape[0][0], cy + shape[0][1]); /* close */
    }
}

void draw_bullets(struct RastPort *rp, Bullet *bullets)
{
    WORD i;
    SetAPen(rp, 1); /* white */
    for (i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) continue;
        {
            WORD bx = FIX_INT(bullets[i].x);
            WORD by = FIX_INT(bullets[i].y);
            /* 2x2 pixel bullet */
            RectFill(rp, bx, by, bx + 1, by + 1);
        }
    }
}

void draw_particles(struct RastPort *rp, Particle *particles)
{
    WORD i;
    for (i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &particles[i];
        WORD px, py;
        if (!p->active) continue;
        px = FIX_INT(p->x);
        py = FIX_INT(p->y);
        if (px >= 0 && px < SCREEN_W && py >= 0 && py < SCREEN_H) {
            SetAPen(rp, p->color);
            WritePixel(rp, px, py);
        }
    }
}

void draw_hud(struct RastPort *rp, GameState *gs)
{
    char buf[40];
    WORD i;

    /* Score */
    SetAPen(rp, 1);
    SetBPen(rp, 0);
    sprintf(buf, "%07ld", (long)gs->score);
    Move(rp, 8, 10);
    Text(rp, buf, strlen(buf));

    /* Lives (small triangles) */
    for (i = 0; i < gs->lives && i < 5; i++) {
        WORD lx = 8 + i * 14;
        WORD ly = 18;
        SetAPen(rp, 1);
        Move(rp, lx + 5, ly);
        Draw(rp, lx - 3, ly + 3);
        Draw(rp, lx - 3, ly - 3);
        Draw(rp, lx + 5, ly);
    }

    /* Level */
    sprintf(buf, "L%ld", (long)gs->level);
    Move(rp, SCREEN_W - 40, 10);
    Text(rp, buf, strlen(buf));
}

/* Draw a thick outlined letter by drawing it offset in multiple directions */
static void draw_big_char(struct RastPort *rp, char ch, WORD x, WORD y,
                          WORD scale)
{
    /* Simple 5x7 bitmap font for uppercase + digits + punctuation */
    /* Each char is 5 columns wide, each column is 7 bits (bit 0 = top) */
    static const UBYTE font5x7[][5] = {
        /* A */ {0x7E,0x11,0x11,0x11,0x7E},
        /* B */ {0x7F,0x49,0x49,0x49,0x36},
        /* C */ {0x3E,0x41,0x41,0x41,0x22},
        /* D */ {0x7F,0x41,0x41,0x41,0x3E},
        /* E */ {0x7F,0x49,0x49,0x49,0x41},
        /* F */ {0x7F,0x09,0x09,0x09,0x01},
        /* G */ {0x3E,0x41,0x49,0x49,0x3A},
        /* H */ {0x7F,0x08,0x08,0x08,0x7F},
        /* I */ {0x00,0x41,0x7F,0x41,0x00},
        /* J */ {0x20,0x40,0x41,0x3F,0x01},
        /* K */ {0x7F,0x08,0x14,0x22,0x41},
        /* L */ {0x7F,0x40,0x40,0x40,0x40},
        /* M */ {0x7F,0x02,0x0C,0x02,0x7F},
        /* N */ {0x7F,0x04,0x08,0x10,0x7F},
        /* O */ {0x3E,0x41,0x41,0x41,0x3E},
        /* P */ {0x7F,0x09,0x09,0x09,0x06},
        /* Q */ {0x3E,0x41,0x51,0x21,0x5E},
        /* R */ {0x7F,0x09,0x19,0x29,0x46},
        /* S */ {0x46,0x49,0x49,0x49,0x31},
        /* T */ {0x01,0x01,0x7F,0x01,0x01},
        /* U */ {0x3F,0x40,0x40,0x40,0x3F},
        /* V */ {0x1F,0x20,0x40,0x20,0x1F},
        /* W */ {0x3F,0x40,0x30,0x40,0x3F},
        /* X */ {0x63,0x14,0x08,0x14,0x63},
        /* Y */ {0x07,0x08,0x70,0x08,0x07},
        /* Z */ {0x61,0x51,0x49,0x45,0x43},
    };
    /* Space and punctuation: ' = 26, ! = 27 */
    static const UBYTE font_apos[5] = {0x00,0x00,0x07,0x00,0x00};

    const UBYTE *glyph = NULL;
    WORD col, row;

    if (ch >= 'A' && ch <= 'Z') glyph = font5x7[ch - 'A'];
    else if (ch >= 'a' && ch <= 'z') glyph = font5x7[ch - 'a'];
    else if (ch == '\'') glyph = font_apos;
    else return; /* skip spaces and unknown */

    for (col = 0; col < 5; col++) {
        UBYTE bits = glyph[col];
        for (row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                WORD px = x + col * scale;
                WORD py = y + row * scale;
                RectFill(rp, px, py, px + scale - 1, py + scale - 1);
            }
        }
    }
}

static WORD draw_big_string(struct RastPort *rp, const char *str,
                            WORD x, WORD y, WORD scale)
{
    while (*str) {
        if (*str == ' ') {
            x += scale * 4;
        } else {
            draw_big_char(rp, *str, x, y, scale);
            x += scale * 6;
        }
        str++;
    }
    return x; /* return end X position */
}

static WORD big_string_width(const char *str, WORD scale)
{
    WORD w = 0;
    while (*str) {
        if (*str == ' ') w += scale * 4;
        else w += scale * 6;
        str++;
    }
    return w - scale; /* subtract trailing gap */
}

void draw_title(struct RastPort *rp)
{
    WORD cx = SCREEN_W / 2;
    WORD w;

    /* Decorative star field (static dots) */
    {
        ULONG seed = 7777;
        WORD i;
        SetAPen(rp, 13); /* dark grey stars */
        for (i = 0; i < 40; i++) {
            seed = seed * 1103515245UL + 12345UL;
            {
                WORD sx = (WORD)((seed >> 16) % SCREEN_W);
                WORD sy = (WORD)((seed >> 3) % SCREEN_H);
                WritePixel(rp, sx, sy);
            }
        }
        SetAPen(rp, 5); /* brighter stars */
        for (i = 0; i < 15; i++) {
            seed = seed * 1103515245UL + 12345UL;
            {
                WORD sx = (WORD)((seed >> 16) % SCREEN_W);
                WORD sy = (WORD)((seed >> 3) % SCREEN_H);
                WritePixel(rp, sx, sy);
            }
        }
    }

    /* --- Title: "CHRIS'S" small above main title --- */
    SetAPen(rp, 9); /* orange */
    w = big_string_width("CHRIS'S", 2);
    draw_big_string(rp, "CHRIS'S", cx - w / 2, 40, 2);

    /* --- Main title: "MEGA" in large yellow --- */
    SetAPen(rp, 10); /* yellow */
    w = big_string_width("MEGA", 4);
    draw_big_string(rp, "MEGA", cx - w / 2, 62, 4);

    /* --- "ASTRO" even bigger in white --- */
    SetAPen(rp, 1); /* white */
    w = big_string_width("ASTRO", 5);
    draw_big_string(rp, "ASTRO", cx - w / 2, 96, 5);

    /* --- "BLASTER" in orange-red --- */
    SetAPen(rp, 9); /* orange */
    w = big_string_width("BLASTER", 4);
    draw_big_string(rp, "BLASTER", cx - w / 2, 138, 4);

    /* Decorative line */
    SetAPen(rp, 2); /* blue */
    Move(rp, 40, 172);
    Draw(rp, SCREEN_W - 40, 172);
    Move(rp, 40, 174);
    Draw(rp, SCREEN_W - 40, 174);

    /* Controls */
    SetAPen(rp, 7); /* light grey */
    SetBPen(rp, 0);
    Move(rp, 60, 186);
    Text(rp, "A/Z  ROTATE LEFT", 16);
    Move(rp, 60, 196);
    Text(rp, "D/C  ROTATE RIGHT", 17);
    Move(rp, 60, 206);
    Text(rp, "W    THRUST", 11);
    Move(rp, 60, 216);
    Text(rp, "SPACE  FIRE", 11);

    /* Joystick note */
    SetAPen(rp, 5);
    Move(rp, 72, 232);
    Text(rp, "JOYSTICK PORT 2 OK", 18);

    /* "PRESS FIRE TO START" blinking */
    SetAPen(rp, 15); /* bright yellow */
    {
        static const char *prompt = "PRESS FIRE TO START";
        WORD pw = 19 * 8;
        Move(rp, cx - pw / 2, 248);
        Text(rp, prompt, 19);
    }
}

void draw_gameover(struct RastPort *rp, LONG score)
{
    char buf[40];
    WORD cx = SCREEN_W / 2;
    WORD w;

    /* "GAME OVER" in big red letters */
    SetAPen(rp, 8); /* red */
    w = big_string_width("GAME OVER", 4);
    draw_big_string(rp, "GAME OVER", cx - w / 2, SCREEN_H / 2 - 40, 4);

    /* Score */
    SetAPen(rp, 1); /* white */
    SetBPen(rp, 0);
    sprintf(buf, "SCORE: %ld", (long)score);
    Move(rp, cx - (WORD)(strlen(buf) * 4), SCREEN_H / 2 + 4);
    Text(rp, buf, strlen(buf));

    /* Prompt */
    SetAPen(rp, 15); /* bright yellow */
    {
        static const char *restart = "PRESS FIRE";
        Move(rp, cx - 40, SCREEN_H / 2 + 24);
        Text(rp, restart, 10);
    }
}
