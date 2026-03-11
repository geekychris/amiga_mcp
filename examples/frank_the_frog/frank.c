/*
 * Frank the Frog - Player (blitter bob with better graphics)
 */

#include <exec/types.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <proto/graphics.h>

#include "game.h"
#include "frank.h"

/* Frank's state */
static int frog_col;    /* grid column (0-19) */
static int frog_row;    /* grid row (0-12) */
static int frog_px;     /* pixel X */
static int frog_py;     /* pixel Y */
static int death_timer; /* death animation countdown */
static int dying;
static int highest_row; /* highest row reached this life (for scoring) */

void frank_init(void)
{
    frog_col = GRID_COLS / 2;
    frog_row = ROW_START;
    frog_px = frog_col * TILE_W;
    frog_py = frog_row * TILE_H;
    death_timer = 0;
    dying = 0;
    highest_row = ROW_START;
}

int frank_highest_row(void) { return highest_row; }

void frank_move(int dx, int dy)
{
    int new_col = frog_col + dx;
    int new_row = frog_row + dy;

    if (dying) return;

    /* Boundary check */
    if (new_col < 0 || new_col >= GRID_COLS) return;
    if (new_row < 0 || new_row >= GRID_ROWS) return;

    frog_col = new_col;
    frog_row = new_row;
    frog_px = frog_col * TILE_W;
    frog_py = frog_row * TILE_H;

    /* Track highest row for forward scoring */
    if (frog_row < highest_row) {
        highest_row = frog_row;
    }
}

void frank_draw(struct RastPort *rp)
{
    int x = frog_px;
    int y = frog_py;

    if (dying) {
        /* Death animation: red X that fades */
        int inset = (30 - death_timer) / 3;
        if (inset > 5) inset = 5;
        SetAPen(rp, (long)COL_CAR_RED);
        /* X pattern */
        Move(rp, x + 2 + inset, y + 2 + inset);
        Draw(rp, x + TILE_W - 3 - inset, y + TILE_H - 3 - inset);
        Move(rp, x + TILE_W - 3 - inset, y + 2 + inset);
        Draw(rp, x + 2 + inset, y + TILE_H - 3 - inset);
        /* Circle of death */
        Move(rp, x + 2, y + 2);
        Draw(rp, x + TILE_W - 3, y + 2);
        Draw(rp, x + TILE_W - 3, y + TILE_H - 3);
        Draw(rp, x + 2, y + TILE_H - 3);
        Draw(rp, x + 2, y + 2);
        return;
    }

    /* Frog body - rounded shape */
    SetAPen(rp, (long)COL_FROG);
    /* Main body */
    RectFill(rp, x + 4, y + 3, x + 11, y + 12);
    /* Head wider */
    RectFill(rp, x + 3, y + 2, x + 12, y + 5);
    /* Bottom */
    RectFill(rp, x + 5, y + 13, x + 10, y + 14);

    /* Eyes (white with black pupil) */
    SetAPen(rp, (long)COL_WHITE);
    RectFill(rp, x + 3, y + 1, x + 5, y + 3);
    RectFill(rp, x + 10, y + 1, x + 12, y + 3);
    SetAPen(rp, (long)COL_BG);
    WritePixel(rp, x + 4, y + 2);
    WritePixel(rp, x + 11, y + 2);

    /* Legs - dark green */
    SetAPen(rp, (long)COL_FROG_DARK);
    /* Front legs (splayed out) */
    RectFill(rp, x + 1, y + 4, x + 3, y + 5);
    RectFill(rp, x + 12, y + 4, x + 14, y + 5);
    /* Back legs (bent) */
    RectFill(rp, x + 1, y + 10, x + 3, y + 13);
    RectFill(rp, x + 12, y + 10, x + 14, y + 13);
    /* Feet */
    RectFill(rp, x + 0, y + 12, x + 2, y + 13);
    RectFill(rp, x + 13, y + 12, x + 15, y + 13);

    /* Mouth line */
    SetAPen(rp, (long)COL_FROG_DARK);
    Move(rp, x + 5, y + 7);
    Draw(rp, x + 10, y + 7);
}

void frank_erase(struct RastPort *rp)
{
    SetAPen(rp, (long)COL_BG);
    RectFill(rp, frog_px, frog_py,
             frog_px + TILE_W - 1, frog_py + TILE_H - 1);
}

int frank_col(void) { return frog_col; }
int frank_row(void) { return frog_row; }
int frank_x(void) { return frog_px; }
int frank_y(void) { return frog_py; }

void frank_set_x(int x)
{
    frog_px = x;
    frog_col = x / TILE_W;
}

void frank_start_death(void)
{
    dying = 1;
    death_timer = 30;
}

int frank_die_tick(void)
{
    if (!dying) return 0;
    death_timer--;
    if (death_timer <= 0) {
        dying = 0;
        return 0;
    }
    return 1;
}

int frank_is_dying(void)
{
    return dying;
}
