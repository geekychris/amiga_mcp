/*
 * Uranus Lander - Drawing routines
 */
#include <proto/exec.h>
#include <proto/graphics.h>
#include <graphics/gfxbase.h>
#include <graphics/gfx.h>
#include <exec/memory.h>
#include <stdio.h>
#include <string.h>
#include "draw.h"
#include "planet_gfx.h"

/* Planet bitmap in chip RAM for blitting */
static struct BitMap planet_bm;
static UBYTE *planet_chip[PLANET_DEPTH] = { NULL };
static WORD planet_ready = 0;

void planet_gfx_init(void)
{
    WORD i;
    InitBitMap(&planet_bm, PLANET_DEPTH, PLANET_W, PLANET_H);
    planet_ready = 1;
    for (i = 0; i < PLANET_DEPTH; i++) {
        planet_chip[i] = (UBYTE *)AllocMem(
            PLANET_ROW_BYTES * PLANET_H, MEMF_CHIP | MEMF_CLEAR);
        if (!planet_chip[i]) { planet_ready = 0; break; }
        CopyMem((APTR)planet_planes[i], planet_chip[i],
                PLANET_ROW_BYTES * PLANET_H);
        planet_bm.Planes[i] = (PLANEPTR)planet_chip[i];
    }
}

void planet_gfx_cleanup(void)
{
    WORD i;
    for (i = 0; i < PLANET_DEPTH; i++) {
        if (planet_chip[i]) {
            FreeMem(planet_chip[i], PLANET_ROW_BYTES * PLANET_H);
            planet_chip[i] = NULL;
        }
    }
    planet_ready = 0;
}

/* Lander shape: 9 vertices, pointing up at angle 0 */
/*   0=antenna, 1-2=right body, 3=right foot, 4=right leg inner,
 *   5=left leg inner, 6=left foot, 7-8=left body */
static const WORD lander_x[] = {  0,  4,  4,  7,  3, -3, -7, -4, -4 };
static const WORD lander_y[] = { -7, -3,  1,  6,  3,  3,  6,  1, -3 };
#define LANDER_VERTS 9

/* Thrust flame triangle */
static const WORD flame_x[] = { -2,  0,  2 };
static const WORD flame_y[] = {  3,  9,  3 };
static const WORD flame_big_y[] = { 3, 12, 3 };
#define FLAME_VERTS 3

/* 5x7 bitmap font: A-Z + 0-9 */
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
    /* 0 */ {0x3E,0x51,0x49,0x45,0x3E},
    /* 1 */ {0x00,0x42,0x7F,0x40,0x00},
    /* 2 */ {0x42,0x61,0x51,0x49,0x46},
    /* 3 */ {0x21,0x41,0x45,0x4B,0x31},
    /* 4 */ {0x18,0x14,0x12,0x7F,0x10},
    /* 5 */ {0x27,0x45,0x45,0x45,0x39},
    /* 6 */ {0x3C,0x4A,0x49,0x49,0x30},
    /* 7 */ {0x01,0x71,0x09,0x05,0x03},
    /* 8 */ {0x36,0x49,0x49,0x49,0x36},
    /* 9 */ {0x06,0x49,0x49,0x29,0x1E},
};

static void draw_big_char(struct RastPort *rp, char ch, WORD x, WORD y, WORD scale)
{
    const UBYTE *glyph = NULL;
    WORD col, row;

    if (ch >= 'A' && ch <= 'Z') glyph = font5x7[ch - 'A'];
    else if (ch >= 'a' && ch <= 'z') glyph = font5x7[ch - 'a'];
    else if (ch >= '0' && ch <= '9') glyph = font5x7[26 + ch - '0'];
    else return;

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

static WORD big_string_width(const char *str, WORD scale)
{
    WORD w = 0;
    while (*str) {
        if (*str == ' ') w += scale * 4;
        else w += scale * 6;
        str++;
    }
    if (w > 0) w -= scale;
    return w;
}

static void draw_big_string(struct RastPort *rp, const char *str,
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
}

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
    SetAPen(rp, COL_BG);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);
}

void draw_stars(struct RastPort *rp, GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_STARS; i++) {
        SetAPen(rp, gs->stars[i].brightness);
        WritePixel(rp, gs->stars[i].x, gs->stars[i].y);
    }
}

void draw_terrain(struct RastPort *rp, GameState *gs)
{
    WORD x, x2, i;

    /* Fill terrain body */
    SetAPen(rp, COL_DKGRAY);
    x = 0;
    while (x < TERRAIN_W) {
        WORD h = gs->terrain_y[x];
        x2 = x;
        while (x2 + 1 < TERRAIN_W && gs->terrain_y[x2 + 1] == h) x2++;
        if (h < SCREEN_H)
            RectFill(rp, x, h, x2, SCREEN_H - 1);
        x = x2 + 1;
    }

    /* Draw surface line */
    SetAPen(rp, COL_LTGRAY);
    Move(rp, 0, gs->terrain_y[0]);
    for (x = 1; x < TERRAIN_W; x++) {
        Draw(rp, x, gs->terrain_y[x]);
    }

    /* Terrain highlight (1px above surface) */
    SetAPen(rp, COL_GRAY);
    for (x = 0; x < TERRAIN_W; x++) {
        WORD ty = gs->terrain_y[x] + 2;
        if (ty < SCREEN_H)
            WritePixel(rp, x, ty);
    }

    /* Landing pads */
    for (i = 0; i < gs->num_pads; i++) {
        LandingPad *p = &gs->pads[i];
        char buf[8];
        WORD pad_cx;

        /* Pad surface - bright yellow band */
        SetAPen(rp, COL_YELLOW);
        RectFill(rp, p->x, p->y - 1, p->x + p->width - 1, p->y);

        /* Pad legs (small vertical lines at edges) */
        SetAPen(rp, COL_YELLOW);
        Move(rp, p->x, p->y); Draw(rp, p->x, p->y + 3);
        Move(rp, p->x + p->width - 1, p->y);
        Draw(rp, p->x + p->width - 1, p->y + 3);

        /* Multiplier label below pad */
        pad_cx = p->x + p->width / 2;
        sprintf(buf, "x%ld", (long)p->multiplier);
        SetAPen(rp, COL_MAGENTA);
        SetBPen(rp, COL_DKGRAY);
        Move(rp, pad_cx - (WORD)(strlen(buf) * 4), p->y + 10);
        Text(rp, buf, strlen(buf));
    }
}

void draw_ship(struct RastPort *rp, GameState *gs)
{
    WORD sx, sy, rx, ry, fx, fy, i;
    Ship *ship = &gs->ship;

    if (!ship->alive) return;

    /* Blink during invulnerability (state_timer used for brief invuln) */
    if (gs->state == STATE_PLAYING && gs->state_timer > 0 && (gs->frame & 4))
        return;

    sx = FIX_INT(ship->x);
    sy = FIX_INT(ship->y);

    /* Clamp to screen for drawing */
    if (sx < -10 || sx > SCREEN_W + 10 || sy < -10 || sy > SCREEN_H + 10)
        return;

    /* Draw lander body */
    SetAPen(rp, COL_CYAN);
    rotate(lander_x[0], lander_y[0], ship->angle, &rx, &ry);
    fx = rx; fy = ry;
    Move(rp, sx + rx, sy + ry);
    for (i = 1; i < LANDER_VERTS; i++) {
        rotate(lander_x[i], lander_y[i], ship->angle, &rx, &ry);
        Draw(rp, sx + rx, sy + ry);
    }
    Draw(rp, sx + fx, sy + fy); /* close polygon */

    /* Draw antenna dot */
    SetAPen(rp, COL_WHITE);
    rotate(0, -8, ship->angle, &rx, &ry);
    if (sx + rx >= 0 && sx + rx < SCREEN_W && sy + ry >= 0 && sy + ry < SCREEN_H)
        WritePixel(rp, sx + rx, sy + ry);

    /* Thrust flame */
    if (ship->thrusting && ship->fuel > 0) {
        const WORD *fy_tab = (gs->frame & 2) ? flame_big_y : flame_y;
        SetAPen(rp, (gs->frame & 4) ? COL_ORANGE : COL_YELLOW);
        rotate(flame_x[0], fy_tab[0], ship->angle, &rx, &ry);
        fx = rx; fy = ry;
        Move(rp, sx + rx, sy + ry);
        for (i = 1; i < FLAME_VERTS; i++) {
            rotate(flame_x[i], fy_tab[i], ship->angle, &rx, &ry);
            Draw(rp, sx + rx, sy + ry);
        }
        Draw(rp, sx + fx, sy + fy);
    }
}

void draw_particles(struct RastPort *rp, GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &gs->particles[i];
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
    WORD fuel_pct, fuel_w, fuel_color;
    Fixed abs_vx, abs_vy;
    WORD vy_color, vx_color;
    WORD alt, i;
    WORD ship_sx, ship_sy;

    SetBPen(rp, COL_BG);

    /* Score - top left */
    SetAPen(rp, COL_BRYELLOW);
    sprintf(buf, "%07ld", (long)gs->score);
    Move(rp, 4, 10);
    Text(rp, buf, strlen(buf));

    /* Level - top right */
    SetAPen(rp, COL_WHITE);
    sprintf(buf, "LVL %ld", (long)gs->level);
    Move(rp, SCREEN_W - (WORD)(strlen(buf) * 8) - 4, 10);
    Text(rp, buf, strlen(buf));

    /* Fuel bar - top center */
    fuel_pct = (gs->ship.fuel > 0) ? (WORD)((gs->ship.fuel * 100L) / g_tune.fuel_max) : 0;
    fuel_w = (WORD)((gs->ship.fuel * 100L) / g_tune.fuel_max);
    if (fuel_w < 0) fuel_w = 0;
    if (fuel_w > 100) fuel_w = 100;

    fuel_color = (fuel_pct > 50) ? COL_GREEN : (fuel_pct > 25) ? COL_YELLOW : COL_RED;

    /* Fuel label */
    SetAPen(rp, COL_SLATE);
    Move(rp, 100, 10);
    Text(rp, "FUEL", 4);

    /* Fuel bar outline */
    SetAPen(rp, COL_GRAY);
    Move(rp, 136, 3); Draw(rp, 237, 3); Draw(rp, 237, 11);
    Draw(rp, 136, 11); Draw(rp, 136, 3);

    /* Fuel bar fill */
    if (fuel_w > 0) {
        SetAPen(rp, fuel_color);
        RectFill(rp, 137, 4, 137 + fuel_w - 1, 10);
    }

    /* Lives - bottom left as small lander icons */
    for (i = 0; i < gs->lives && i < 5; i++) {
        WORD lx = 8 + i * 12;
        WORD ly = SCREEN_H - 10;
        SetAPen(rp, COL_CYAN);
        /* Tiny lander: triangle + legs */
        Move(rp, lx, ly - 4);
        Draw(rp, lx + 3, ly);
        Draw(rp, lx - 3, ly);
        Draw(rp, lx, ly - 4);
        Move(rp, lx - 3, ly); Draw(rp, lx - 4, ly + 2);
        Move(rp, lx + 3, ly); Draw(rp, lx + 4, ly + 2);
    }

    /* Altitude - bottom center */
    ship_sx = FIX_INT(gs->ship.x);
    ship_sy = FIX_INT(gs->ship.y);
    if (ship_sx >= 0 && ship_sx < TERRAIN_W)
        alt = gs->terrain_y[ship_sx] - ship_sy - 6;
    else
        alt = 0;
    if (alt < 0) alt = 0;

    SetAPen(rp, COL_SLATE);
    sprintf(buf, "ALT:%ld", (long)alt);
    Move(rp, SCREEN_W / 2 - 24, SCREEN_H - 4);
    Text(rp, buf, strlen(buf));

    /* Speed indicators - bottom right */
    abs_vx = gs->ship.vx < 0 ? -gs->ship.vx : gs->ship.vx;
    abs_vy = gs->ship.vy < 0 ? -gs->ship.vy : gs->ship.vy;

    vy_color = (abs_vy < g_tune.safe_vy) ? COL_GREEN :
               (abs_vy < g_tune.safe_vy * 3 / 2) ? COL_YELLOW : COL_RED;
    vx_color = (abs_vx < g_tune.safe_vx) ? COL_GREEN :
               (abs_vx < g_tune.safe_vx * 3 / 2) ? COL_YELLOW : COL_RED;

    {
        WORD vx_disp = (WORD)(gs->ship.vx >> 10);
        WORD vy_disp = (WORD)(gs->ship.vy >> 10);
        SetAPen(rp, vx_color);
        sprintf(buf, "H:%ld", (long)vx_disp);
        Move(rp, SCREEN_W - 80, SCREEN_H - 4);
        Text(rp, buf, strlen(buf));

        SetAPen(rp, vy_color);
        sprintf(buf, "V:%ld", (long)vy_disp);
        Move(rp, SCREEN_W - 40, SCREEN_H - 4);
        Text(rp, buf, strlen(buf));
    }
}

/* Blit the planet bitmap onto the screen */
static void draw_planet_bm(struct RastPort *rp, WORD x, WORD y)
{
    if (planet_ready) {
        BltBitMapRastPort(&planet_bm, 0, 0, rp, x, y,
                          PLANET_W, PLANET_H, 0xC0);
    }
}

/* Color cycle table for flickering URANUS text */
static const WORD flicker_colors[] = {
    COL_CYAN, COL_BRWHITE, COL_CYAN, COL_DKCYAN,
    COL_CYAN, COL_CYAN, COL_BRWHITE, COL_CYAN,
    COL_DKCYAN, COL_CYAN, COL_BRWHITE, COL_BRWHITE,
    COL_CYAN, COL_CYAN, COL_DKCYAN, COL_CYAN,
};
#define FLICKER_LEN 16

void draw_title(struct RastPort *rp, GameState *gs)
{
    WORD cx = SCREEN_W / 2;
    WORD w, i, y;
    WORD flicker_col;

    /* Moving stars */
    draw_stars(rp, gs);

    /* Planet Uranus bitmap - positioned top right */
    draw_planet_bm(rp, SCREEN_W - PLANET_W - 8, 6);

    /* Title: URANUS - flickering color */
    flicker_col = flicker_colors[(gs->frame >> 2) & (FLICKER_LEN - 1)];
    SetAPen(rp, flicker_col);
    w = big_string_width("URANUS", 3);
    draw_big_string(rp, "URANUS", cx - w / 2 - 20, 30, 3);

    /* Title: LANDER */
    SetAPen(rp, COL_BRYELLOW);
    w = big_string_width("LANDER", 3);
    draw_big_string(rp, "LANDER", cx - w / 2 - 20, 58, 3);

    /* Decorative lines */
    SetAPen(rp, COL_DKCYAN);
    Move(rp, 30, 88); Draw(rp, SCREEN_W - 30, 88);
    Move(rp, 30, 90); Draw(rp, SCREEN_W - 30, 90);

    /* High scores */
    SetAPen(rp, COL_WHITE);
    SetBPen(rp, COL_BG);
    Move(rp, cx - 40, 105);
    Text(rp, "TOP SCORES", 10);

    y = 118;
    for (i = 0; i < MAX_HISCORES && i < 8; i++) {
        char line[32];
        SetAPen(rp, (i < 3) ? COL_YELLOW : COL_LTGRAY);
        sprintf(line, "%ld. %s  %06ld",
                (long)(i + 1),
                gs->hiscores[i].name,
                (long)gs->hiscores[i].score);
        Move(rp, 80, y);
        Text(rp, line, strlen(line));
        y += 11;
    }

    /* Controls */
    SetAPen(rp, COL_GRAY);
    Move(rp, 50, 208);
    Text(rp, "ARROWS/JOY  ROTATE", 18);
    Move(rp, 50, 219);
    Text(rp, "FIRE/SPACE  THRUST", 18);
    Move(rp, 50, 230);
    Text(rp, "M  MUSIC ON/OFF", 15);

    /* Press fire prompt */
    if (gs->frame & 32) {
        SetAPen(rp, COL_BRWHITE);
        Move(rp, cx - 68, 246);
        Text(rp, "PRESS FIRE TO START", 19);
    }
}

void draw_landed(struct RastPort *rp, GameState *gs)
{
    char buf[32];
    WORD cx = SCREEN_W / 2;

    SetBPen(rp, COL_BG);

    /* "LANDED!" text */
    SetAPen(rp, COL_GREEN);
    Move(rp, cx - 28, 40);
    Text(rp, "LANDED!", 7);

    /* Bonus info */
    SetAPen(rp, COL_BRYELLOW);
    if (gs->landed_pad >= 0 && gs->landed_pad < gs->num_pads) {
        sprintf(buf, "x%ld BONUS: %ld",
                (long)gs->pads[gs->landed_pad].multiplier,
                (long)gs->land_bonus);
    } else {
        sprintf(buf, "BONUS: %ld", (long)gs->land_bonus);
    }
    Move(rp, cx - (WORD)(strlen(buf) * 4), 56);
    Text(rp, buf, strlen(buf));
}

void draw_crash(struct RastPort *rp, GameState *gs)
{
    WORD cx = SCREEN_W / 2;
    (void)gs;

    SetBPen(rp, COL_BG);
    SetAPen(rp, COL_RED);
    Move(rp, cx - 36, 40);
    Text(rp, "DESTROYED!", 10);
}

void draw_gameover(struct RastPort *rp, GameState *gs)
{
    char buf[32];
    WORD cx = SCREEN_W / 2;

    SetBPen(rp, COL_BG);

    SetAPen(rp, COL_RED);
    Move(rp, cx - 36, SCREEN_H / 2 - 20);
    Text(rp, "GAME  OVER", 10);

    SetAPen(rp, COL_WHITE);
    sprintf(buf, "SCORE: %ld", (long)gs->score);
    Move(rp, cx - (WORD)(strlen(buf) * 4), SCREEN_H / 2);
    Text(rp, buf, strlen(buf));

    if (gs->frame & 32) {
        SetAPen(rp, COL_BRYELLOW);
        Move(rp, cx - 40, SCREEN_H / 2 + 24);
        Text(rp, "PRESS FIRE", 10);
    }
}

void draw_enter_name(struct RastPort *rp, GameState *gs)
{
    WORD cx = SCREEN_W / 2;
    WORD i, lx;

    SetBPen(rp, COL_BG);

    /* "NEW HIGH SCORE!" */
    SetAPen(rp, COL_BRYELLOW);
    Move(rp, cx - 60, SCREEN_H / 2 - 40);
    Text(rp, "NEW HIGH SCORE!", 15);

    /* Score */
    {
        char buf[20];
        SetAPen(rp, COL_WHITE);
        sprintf(buf, "%ld", (long)gs->score);
        Move(rp, cx - (WORD)(strlen(buf) * 4), SCREEN_H / 2 - 24);
        Text(rp, buf, strlen(buf));
    }

    /* "ENTER YOUR NAME:" */
    SetAPen(rp, COL_GRAY);
    Move(rp, cx - 64, SCREEN_H / 2 - 4);
    Text(rp, "ENTER YOUR NAME:", 16);

    /* Letter slots */
    lx = cx - 20;
    for (i = 0; i < 3; i++) {
        char ch = gs->name_buf[i];
        WORD x = lx + i * 16;
        WORD y = SCREEN_H / 2 + 14;

        /* Highlight current position */
        if (i == gs->name_pos) {
            SetAPen(rp, COL_YELLOW);
            /* Blinking cursor */
            if (gs->frame & 8) {
                RectFill(rp, x - 1, y - 8, x + 7, y + 1);
                SetAPen(rp, COL_BG);
            } else {
                SetAPen(rp, COL_BRWHITE);
            }
        } else {
            SetAPen(rp, COL_WHITE);
        }

        Move(rp, x, y);
        Text(rp, &ch, 1);

        /* Underline */
        SetAPen(rp, COL_GRAY);
        Move(rp, x - 1, y + 2);
        Draw(rp, x + 7, y + 2);
    }

    /* Up/down arrows hint */
    SetAPen(rp, COL_SLATE);
    Move(rp, cx - 48, SCREEN_H / 2 + 36);
    Text(rp, "UP/DN  FIRE=OK", 14);
}
