/*
 * Gold Rush - Rendering engine
 *
 * All drawing uses Amiga RastPort functions.
 * Pen/coordinate args cast to (long) for Amiga varargs compatibility.
 * sprintf returns char* (not int) under amiga.lib - use strlen() after.
 */

#include <exec/types.h>
#include <graphics/rastport.h>
#include <proto/graphics.h>

#include <stdio.h>
#include <string.h>

#include "game.h"
#include "render.h"

/* ------------------------------------------------------------------ */
/*  Helper: draw a rectangle outline using Move/Draw                   */
/* ------------------------------------------------------------------ */
static void draw_rect_outline(struct RastPort *rp,
                              int x1, int y1, int x2, int y2)
{
    Move(rp, (long)x1, (long)y1);
    Draw(rp, (long)x2, (long)y1);
    Draw(rp, (long)x2, (long)y2);
    Draw(rp, (long)x1, (long)y2);
    Draw(rp, (long)x1, (long)y1);
}

/* ------------------------------------------------------------------ */
/*  Tile drawing helpers                                               */
/* ------------------------------------------------------------------ */

static void draw_brick(struct RastPort *rp, int sx, int sy)
{
    /* Fill entire tile with brick color */
    SetAPen(rp, (long)COL_BRICK);
    RectFill(rp, (long)sx, (long)sy,
             (long)(sx + TILE_W - 1), (long)(sy + TILE_H - 1));

    /* Mortar lines (horizontal) */
    SetAPen(rp, (long)COL_BRICK_HI);

    /* Row 0 mortar */
    Move(rp, (long)sx, (long)(sy + 3));
    Draw(rp, (long)(sx + TILE_W - 1), (long)(sy + 3));

    /* Row 1 mortar */
    Move(rp, (long)sx, (long)(sy + 7));
    Draw(rp, (long)(sx + TILE_W - 1), (long)(sy + 7));

    /* Row 2 mortar */
    Move(rp, (long)sx, (long)(sy + 11));
    Draw(rp, (long)(sx + TILE_W - 1), (long)(sy + 11));

    /* Vertical mortar: offset brick pattern */
    /* Rows 0 and 2: vertical line at center */
    Move(rp, (long)(sx + 5), (long)sy);
    Draw(rp, (long)(sx + 5), (long)(sy + 3));
    Move(rp, (long)(sx + 5), (long)(sy + 8));
    Draw(rp, (long)(sx + 5), (long)(sy + 11));

    /* Row 1: offset vertical lines */
    Move(rp, (long)(sx + 2), (long)(sy + 4));
    Draw(rp, (long)(sx + 2), (long)(sy + 7));
    Move(rp, (long)(sx + 8), (long)(sy + 4));
    Draw(rp, (long)(sx + 8), (long)(sy + 7));
}

static void draw_solid(struct RastPort *rp, int sx, int sy)
{
    /* Fill tile */
    SetAPen(rp, (long)COL_SOLID);
    RectFill(rp, (long)sx, (long)sy,
             (long)(sx + TILE_W - 1), (long)(sy + TILE_H - 1));

    /* Highlight at top-left corner (2x2) */
    SetAPen(rp, (long)COL_SOLID_HI);
    RectFill(rp, (long)sx, (long)sy,
             (long)(sx + 1), (long)(sy + 1));

    /* Shadow at bottom-right (1px lines) */
    SetAPen(rp, (long)COL_BG);
    Move(rp, (long)sx, (long)(sy + TILE_H - 1));
    Draw(rp, (long)(sx + TILE_W - 1), (long)(sy + TILE_H - 1));
    Move(rp, (long)(sx + TILE_W - 1), (long)sy);
    Draw(rp, (long)(sx + TILE_W - 1), (long)(sy + TILE_H - 1));
}

static void draw_ladder(struct RastPort *rp, int sx, int sy)
{
    SetAPen(rp, (long)COL_LADDER);

    /* Two vertical rails */
    Move(rp, (long)(sx + 2), (long)sy);
    Draw(rp, (long)(sx + 2), (long)(sy + TILE_H - 1));
    Move(rp, (long)(sx + 7), (long)sy);
    Draw(rp, (long)(sx + 7), (long)(sy + TILE_H - 1));

    /* Horizontal rungs */
    Move(rp, (long)(sx + 2), (long)(sy + 2));
    Draw(rp, (long)(sx + 7), (long)(sy + 2));
    Move(rp, (long)(sx + 2), (long)(sy + 6));
    Draw(rp, (long)(sx + 7), (long)(sy + 6));
    Move(rp, (long)(sx + 2), (long)(sy + 10));
    Draw(rp, (long)(sx + 7), (long)(sy + 10));
}

static void draw_bar(struct RastPort *rp, int sx, int sy)
{
    int hx;

    SetAPen(rp, (long)COL_BAR);

    /* Horizontal bar: two lines */
    Move(rp, (long)sx, (long)(sy + 2));
    Draw(rp, (long)(sx + TILE_W - 1), (long)(sy + 2));
    Move(rp, (long)sx, (long)(sy + 3));
    Draw(rp, (long)(sx + TILE_W - 1), (long)(sy + 3));

    /* Small vertical hangers every 3 pixels */
    for (hx = sx + 1; hx < sx + TILE_W; hx += 3) {
        Move(rp, (long)hx, (long)(sy + 4));
        Draw(rp, (long)hx, (long)(sy + 5));
    }
}

static void draw_gold(struct RastPort *rp, int sx, int sy)
{
    /* Gold nugget shape */
    SetAPen(rp, (long)COL_GOLD);
    RectFill(rp, (long)(sx + 2), (long)(sy + 4),
             (long)(sx + 7), (long)(sy + 11));

    /* Highlight pixel */
    SetAPen(rp, (long)COL_GOLD_HI);
    WritePixel(rp, (long)(sx + 3), (long)(sy + 5));

    /* Small shine/detail */
    SetAPen(rp, (long)COL_BG);
    WritePixel(rp, (long)(sx + 5), (long)(sy + 7));
    WritePixel(rp, (long)(sx + 4), (long)(sy + 8));
}

/* ------------------------------------------------------------------ */
/*  render_playfield                                                   */
/* ------------------------------------------------------------------ */
void render_playfield(struct RastPort *rp, GameState *gs)
{
    int x, y;
    int sx, sy;
    UBYTE tile;

    /* Clear entire screen */
    SetAPen(rp, (long)COL_BG);
    RectFill(rp, 0L, 0L, (long)(SCREEN_W - 1), (long)(SCREEN_H - 1));

    /* Draw side borders (dark blue) */
    SetAPen(rp, (long)COL_HUD_BG);
    RectFill(rp, 0L, 0L, (long)(PLAYFIELD_X - 1), (long)(STATUS_Y - 1));
    RectFill(rp, (long)(PLAYFIELD_X + PLAYFIELD_W), 0L,
             (long)(SCREEN_W - 1), (long)(STATUS_Y - 1));

    /* Draw each tile */
    for (x = 0; x < GRID_COLS; x++) {
        for (y = 0; y < GRID_ROWS; y++) {
            tile = gs->tiles[x][y];
            sx = PLAYFIELD_X + x * TILE_W;
            sy = PLAYFIELD_Y + y * TILE_H;

            switch (tile) {
            case TILE_BRICK:
            case TILE_TRAP:
                draw_brick(rp, sx, sy);
                break;

            case TILE_SOLID:
                draw_solid(rp, sx, sy);
                break;

            case TILE_LADDER:
                draw_ladder(rp, sx, sy);
                break;

            case TILE_BAR:
                draw_bar(rp, sx, sy);
                break;

            case TILE_GOLD:
                draw_gold(rp, sx, sy);
                break;

            case TILE_HIDDEN_LADDER:
                /* Invisible until revealed - draw as empty */
                break;

            case TILE_EMPTY:
            default:
                /* Already cleared by initial fill */
                break;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  render_entities                                                     */
/* ------------------------------------------------------------------ */

static void draw_player(struct RastPort *rp, GameState *gs)
{
    int sx, sy;
    int anim;

    sx = PLAYFIELD_X + gs->player.px;
    sy = PLAYFIELD_Y + gs->player.py;
    anim = gs->player.anim;

    /* Dead: flash between player color and background */
    if (gs->player.state == PS_DEAD) {
        if ((gs->frame / 4) % 2) {
            return; /* invisible on odd phases */
        }
    }

    /* Body */
    SetAPen(rp, (long)COL_PLAYER);
    RectFill(rp, (long)(sx + 2), (long)(sy + 2),
             (long)(sx + 7), (long)(sy + 11));

    /* Head */
    SetAPen(rp, (long)COL_PLAYER_HI);
    RectFill(rp, (long)(sx + 3), (long)(sy + 1),
             (long)(sx + 6), (long)(sy + 3));

    /* Legs: alternate position for running animation */
    SetAPen(rp, (long)COL_PLAYER);
    if (gs->player.state == PS_RUNNING) {
        if ((anim / 4) % 2) {
            /* Stride pose 1 */
            RectFill(rp, (long)(sx + 2), (long)(sy + 10),
                     (long)(sx + 3), (long)(sy + 13));
            RectFill(rp, (long)(sx + 6), (long)(sy + 10),
                     (long)(sx + 7), (long)(sy + 12));
        } else {
            /* Stride pose 2 */
            RectFill(rp, (long)(sx + 2), (long)(sy + 10),
                     (long)(sx + 3), (long)(sy + 12));
            RectFill(rp, (long)(sx + 6), (long)(sy + 10),
                     (long)(sx + 7), (long)(sy + 13));
        }
    } else {
        /* Standing */
        RectFill(rp, (long)(sx + 2), (long)(sy + 10),
                 (long)(sx + 3), (long)(sy + 13));
        RectFill(rp, (long)(sx + 6), (long)(sy + 10),
                 (long)(sx + 7), (long)(sy + 13));
    }

    /* Arms depend on state */
    SetAPen(rp, (long)COL_PLAYER_HI);
    switch (gs->player.state) {
    case PS_CLIMBING:
        /* Alternating arm positions for climbing */
        if ((anim / 4) % 2) {
            RectFill(rp, (long)(sx + 0), (long)(sy + 3),
                     (long)(sx + 1), (long)(sy + 5));
            RectFill(rp, (long)(sx + 8), (long)(sy + 5),
                     (long)(sx + 9), (long)(sy + 7));
        } else {
            RectFill(rp, (long)(sx + 0), (long)(sy + 5),
                     (long)(sx + 1), (long)(sy + 7));
            RectFill(rp, (long)(sx + 8), (long)(sy + 3),
                     (long)(sx + 9), (long)(sy + 5));
        }
        break;

    case PS_HANGING:
        /* Arms up, body stretched */
        RectFill(rp, (long)(sx + 1), (long)(sy + 0),
                 (long)(sx + 2), (long)(sy + 3));
        RectFill(rp, (long)(sx + 7), (long)(sy + 0),
                 (long)(sx + 8), (long)(sy + 3));
        break;

    case PS_DIGGING:
        /* Extended arm to dig side */
        if (gs->player.dir == DIR_LEFT) {
            RectFill(rp, (long)(sx - 2), (long)(sy + 6),
                     (long)(sx + 1), (long)(sy + 7));
        } else {
            RectFill(rp, (long)(sx + 8), (long)(sy + 6),
                     (long)(sx + 11), (long)(sy + 7));
        }
        /* Other arm normal */
        RectFill(rp, (long)(sx + 0), (long)(sy + 4),
                 (long)(sx + 1), (long)(sy + 6));
        break;

    default:
        /* Normal arms at sides */
        RectFill(rp, (long)(sx + 0), (long)(sy + 4),
                 (long)(sx + 1), (long)(sy + 6));
        RectFill(rp, (long)(sx + 8), (long)(sy + 4),
                 (long)(sx + 9), (long)(sy + 6));
        break;
    }
}

static void draw_enemy(struct RastPort *rp, GameState *gs, int idx)
{
    Enemy *e;
    int sx, sy;
    int anim;

    e = &gs->enemies[idx];
    if (!e->active) return;

    sx = PLAYFIELD_X + e->px;
    sy = PLAYFIELD_Y + e->py;
    anim = e->anim;

    /* Trapped: only draw upper half, offset down into the brick */
    if (e->state == ES_TRAPPED) {
        /* Head poking out of brick */
        SetAPen(rp, (long)COL_ENEMY_HI);
        RectFill(rp, (long)(sx + 3), (long)(sy + 0),
                 (long)(sx + 6), (long)(sy + 3));

        /* Upper body visible */
        SetAPen(rp, (long)COL_ENEMY);
        RectFill(rp, (long)(sx + 2), (long)(sy + 3),
                 (long)(sx + 7), (long)(sy + 6));

        /* If carrying gold, show small gold dot */
        if (e->has_gold) {
            SetAPen(rp, (long)COL_GOLD);
            WritePixel(rp, (long)(sx + 4), (long)(sy + 4));
            WritePixel(rp, (long)(sx + 5), (long)(sy + 4));
        }
        return;
    }

    /* Full enemy body */
    SetAPen(rp, (long)COL_ENEMY);
    RectFill(rp, (long)(sx + 2), (long)(sy + 2),
             (long)(sx + 7), (long)(sy + 11));

    /* Head */
    SetAPen(rp, (long)COL_ENEMY_HI);
    RectFill(rp, (long)(sx + 3), (long)(sy + 1),
             (long)(sx + 6), (long)(sy + 3));

    /* Legs with animation */
    SetAPen(rp, (long)COL_ENEMY);
    if (e->state == ES_RUNNING) {
        if ((anim / 4) % 2) {
            RectFill(rp, (long)(sx + 2), (long)(sy + 10),
                     (long)(sx + 3), (long)(sy + 13));
            RectFill(rp, (long)(sx + 6), (long)(sy + 10),
                     (long)(sx + 7), (long)(sy + 12));
        } else {
            RectFill(rp, (long)(sx + 2), (long)(sy + 10),
                     (long)(sx + 3), (long)(sy + 12));
            RectFill(rp, (long)(sx + 6), (long)(sy + 10),
                     (long)(sx + 7), (long)(sy + 13));
        }
    } else {
        /* Standing legs */
        RectFill(rp, (long)(sx + 2), (long)(sy + 10),
                 (long)(sx + 3), (long)(sy + 13));
        RectFill(rp, (long)(sx + 6), (long)(sy + 10),
                 (long)(sx + 7), (long)(sy + 13));
    }

    /* Arms */
    SetAPen(rp, (long)COL_ENEMY_HI);
    if (e->state == ES_HANGING) {
        /* Arms up */
        RectFill(rp, (long)(sx + 1), (long)(sy + 0),
                 (long)(sx + 2), (long)(sy + 3));
        RectFill(rp, (long)(sx + 7), (long)(sy + 0),
                 (long)(sx + 8), (long)(sy + 3));
    } else if (e->state == ES_CLIMBING) {
        /* Alternating climb arms */
        if ((anim / 4) % 2) {
            RectFill(rp, (long)(sx + 0), (long)(sy + 3),
                     (long)(sx + 1), (long)(sy + 5));
            RectFill(rp, (long)(sx + 8), (long)(sy + 5),
                     (long)(sx + 9), (long)(sy + 7));
        } else {
            RectFill(rp, (long)(sx + 0), (long)(sy + 5),
                     (long)(sx + 1), (long)(sy + 7));
            RectFill(rp, (long)(sx + 8), (long)(sy + 3),
                     (long)(sx + 9), (long)(sy + 5));
        }
    } else {
        /* Normal arms */
        RectFill(rp, (long)(sx + 0), (long)(sy + 4),
                 (long)(sx + 1), (long)(sy + 6));
        RectFill(rp, (long)(sx + 8), (long)(sy + 4),
                 (long)(sx + 9), (long)(sy + 6));
    }

    /* Gold indicator if carrying */
    if (e->has_gold) {
        SetAPen(rp, (long)COL_GOLD);
        RectFill(rp, (long)(sx + 4), (long)(sy + 5),
                 (long)(sx + 5), (long)(sy + 6));
    }
}

void render_entities(struct RastPort *rp, GameState *gs)
{
    int i;

    /* Draw enemies first so player draws on top */
    for (i = 0; i < gs->num_enemies; i++) {
        draw_enemy(rp, gs, i);
    }

    /* Draw player */
    draw_player(rp, gs);
}

/* ------------------------------------------------------------------ */
/*  render_status                                                      */
/* ------------------------------------------------------------------ */
void render_status(struct RastPort *rp, GameState *gs)
{
    char buf[40];

    /* Background */
    SetAPen(rp, (long)COL_HUD_BG);
    RectFill(rp, 0L, (long)STATUS_Y,
             (long)(SCREEN_W - 1), (long)(SCREEN_H - 1));

    /* Top border line */
    SetAPen(rp, (long)COL_LADDER);
    Move(rp, 0L, (long)STATUS_Y);
    Draw(rp, (long)(SCREEN_W - 1), (long)STATUS_Y);

    /* Text rendering setup */
    SetBPen(rp, (long)COL_HUD_BG);

    /* Score at left */
    SetAPen(rp, (long)COL_TEXT);
    sprintf(buf, "SCORE:%06ld", (long)gs->score);
    Move(rp, 8L, (long)(STATUS_Y + 10));
    Text(rp, (CONST_STRPTR)buf, (long)strlen(buf));

    /* Level at center */
    sprintf(buf, "LV:%ld", (long)gs->level_num);
    Move(rp, 120L, (long)(STATUS_Y + 10));
    Text(rp, (CONST_STRPTR)buf, (long)strlen(buf));

    /* Lives at right */
    sprintf(buf, "LIVES:%ld", (long)gs->lives);
    Move(rp, 216L, (long)(STATUS_Y + 10));
    Text(rp, (CONST_STRPTR)buf, (long)strlen(buf));

    /* Gold count below center */
    SetAPen(rp, (long)COL_GOLD);
    sprintf(buf, "GOLD:%ld/%ld", (long)gs->gold_collected,
            (long)gs->gold_total);
    Move(rp, 112L, (long)(STATUS_Y + 20));
    Text(rp, (CONST_STRPTR)buf, (long)strlen(buf));
}

/* ------------------------------------------------------------------ */
/*  render_title                                                       */
/* ------------------------------------------------------------------ */
void render_title(struct RastPort *rp, GameState *gs)
{
    /* Clear screen */
    SetAPen(rp, (long)COL_BG);
    RectFill(rp, 0L, 0L, (long)(SCREEN_W - 1), (long)(SCREEN_H - 1));

    /* Title text */
    SetBPen(rp, (long)COL_BG);
    SetAPen(rp, (long)COL_GOLD);
    Move(rp, (long)((SCREEN_W - 11 * 8) / 2), 60L);
    Text(rp, (CONST_STRPTR)"GOLD RUSH", 11L);

    /* Decorative brick line */
    SetAPen(rp, (long)COL_BRICK);
    RectFill(rp, 40L, 70L, 280L, 72L);
    SetAPen(rp, (long)COL_BRICK_HI);
    RectFill(rp, 40L, 70L, 280L, 70L);

    /* Subtitle */
    SetAPen(rp, (long)COL_LADDER);
    Move(rp, (long)((SCREEN_W - 9 * 8) / 2), 100L);
    Text(rp, (CONST_STRPTR)"for Amiga", 9L);

    /* Start prompt - blink based on frame counter */
    if ((gs->frame / 25) % 2) {
        SetAPen(rp, (long)COL_TEXT);
        Move(rp, (long)((SCREEN_W - 20 * 8) / 2), 140L);
        Text(rp, (CONST_STRPTR)"PRESS FIRE TO START ", 20L);
    }

    /* Controls */
    SetAPen(rp, (long)COL_PLAYER);
    Move(rp, (long)((SCREEN_W - 20 * 8) / 2), 170L);
    Text(rp, (CONST_STRPTR)"Joy/Arrows  Move    ", 20L);
    Move(rp, (long)((SCREEN_W - 20 * 8) / 2), 185L);
    Text(rp, (CONST_STRPTR)"Fire+Dir    Dig     ", 20L);
    Move(rp, (long)((SCREEN_W - 20 * 8) / 2), 200L);
    Text(rp, (CONST_STRPTR)"E           Editor  ", 20L);
    Move(rp, (long)((SCREEN_W - 20 * 8) / 2), 215L);
    Text(rp, (CONST_STRPTR)"ESC         Quit    ", 20L);

    /* Preview elements with labels */
    SetAPen(rp, (long)COL_TEXT);
    SetBPen(rp, (long)COL_BG);

    /* Small brick preview */
    draw_brick(rp, 60, 230);
    Move(rp, 74L, 240L);
    Text(rp, (CONST_STRPTR)"Brick", 5L);

    /* Small ladder preview */
    draw_ladder(rp, 120, 230);
    Move(rp, 134L, 240L);
    Text(rp, (CONST_STRPTR)"Ladder", 6L);

    /* Small gold preview */
    draw_gold(rp, 190, 230);
    Move(rp, 204L, 240L);
    Text(rp, (CONST_STRPTR)"Gold", 4L);
}

/* ------------------------------------------------------------------ */
/*  render_gameover                                                    */
/* ------------------------------------------------------------------ */
void render_gameover(struct RastPort *rp, GameState *gs)
{
    char buf[40];

    /* Dark overlay box */
    SetAPen(rp, (long)COL_BG);
    RectFill(rp, 60L, 80L, 260L, 170L);

    /* Border */
    SetAPen(rp, (long)COL_ENEMY);
    draw_rect_outline(rp, 60, 80, 260, 170);

    /* "GAME OVER" in red */
    SetBPen(rp, (long)COL_BG);
    SetAPen(rp, (long)COL_ENEMY);
    Move(rp, (long)((SCREEN_W - 9 * 8) / 2), 105L);
    Text(rp, (CONST_STRPTR)"GAME OVER", 9L);

    /* Score */
    SetAPen(rp, (long)COL_TEXT);
    sprintf(buf, "SCORE: %06ld", (long)gs->score);
    Move(rp, (long)((SCREEN_W - (int)strlen(buf) * 8) / 2), 130L);
    Text(rp, (CONST_STRPTR)buf, (long)strlen(buf));

    /* Level reached */
    sprintf(buf, "LEVEL: %ld", (long)gs->level_num);
    Move(rp, (long)((SCREEN_W - (int)strlen(buf) * 8) / 2), 145L);
    Text(rp, (CONST_STRPTR)buf, (long)strlen(buf));

    /* Continue prompt */
    SetAPen(rp, (long)COL_GOLD);
    Move(rp, (long)((SCREEN_W - 22 * 8) / 2), 165L);
    Text(rp, (CONST_STRPTR)"PRESS FIRE TO CONTINUE", 22L);
}

/* ------------------------------------------------------------------ */
/*  render_level_done                                                  */
/* ------------------------------------------------------------------ */
void render_level_done(struct RastPort *rp, GameState *gs)
{
    char buf[40];
    (void)gs; /* gs available for future use */

    /* Overlay box */
    SetAPen(rp, (long)COL_BG);
    RectFill(rp, 70L, 85L, 250L, 155L);

    /* Border */
    SetAPen(rp, (long)COL_GOLD);
    draw_rect_outline(rp, 70, 85, 250, 155);

    /* "LEVEL COMPLETE!" */
    SetBPen(rp, (long)COL_BG);
    SetAPen(rp, (long)COL_GOLD);
    Move(rp, (long)((SCREEN_W - 15 * 8) / 2), 110L);
    Text(rp, (CONST_STRPTR)"LEVEL COMPLETE!", 15L);

    /* Bonus text */
    sprintf(buf, "+1000 BONUS!");
    SetAPen(rp, (long)COL_PLAYER);
    Move(rp, (long)((SCREEN_W - (int)strlen(buf) * 8) / 2), 140L);
    Text(rp, (CONST_STRPTR)buf, (long)strlen(buf));
}
