/*
 * draw.c - Rendering: player, guests, items, HUD, screens
 */
#include <proto/graphics.h>
#include <graphics/rastport.h>
#include <string.h>
#include "game.h"
#include "rj_sprite.h"

/* Convert world X to screen X */
#define W2S(gs, wx) ((wx) - (gs)->camera_x)

/* Check if world X is on screen */
#define ON_SCREEN(gs, wx, w) (W2S(gs, wx) + (w) > 0 && W2S(gs, wx) < SCREEN_W)

/* Draw RJ head bitmap from planar data using color-keyed rectangles.
 * Groups same-color horizontal runs to minimize draw calls. */
static void draw_rj_head(struct RastPort *rp, WORD dx, WORD dy)
{
    WORD x, y;
    for (y = 0; y < RJ_HEAD_H; y++) {
        const UBYTE *row = &rj_head_data[y * RJ_HEAD_BPR * 4];
        WORD sy = dy + y;
        WORD run_color = -1;
        WORD run_start = 0;

        if (sy < 0 || sy >= SCREEN_H) continue;

        for (x = 0; x <= RJ_HEAD_W; x++) {
            WORD color = COL_BG;
            if (x < RJ_HEAD_W) {
                WORD byte_idx = x >> 3;
                WORD bit = 7 - (x & 7);
                color = 0;
                if (row[0 * RJ_HEAD_BPR + byte_idx] & (1 << bit)) color |= 1;
                if (row[1 * RJ_HEAD_BPR + byte_idx] & (1 << bit)) color |= 2;
                if (row[2 * RJ_HEAD_BPR + byte_idx] & (1 << bit)) color |= 4;
                if (row[3 * RJ_HEAD_BPR + byte_idx] & (1 << bit)) color |= 8;
            }

            if (color != run_color) {
                /* Flush previous run */
                if (run_color != COL_BG && run_color >= 0) {
                    WORD rx1 = dx + run_start;
                    WORD rx2 = dx + x - 1;
                    if (rx1 < 0) rx1 = 0;
                    if (rx2 >= SCREEN_W) rx2 = SCREEN_W - 1;
                    if (rx1 <= rx2) {
                        SetAPen(rp, run_color);
                        RectFill(rp, rx1, sy, rx2, sy);
                    }
                }
                run_color = color;
                run_start = x;
            }
        }
    }
}

void draw_clear(struct RastPort *rp)
{
    SetRast(rp, COL_BG);
}

void draw_text(struct RastPort *rp, WORD x, WORD y, const char *str)
{
    Move(rp, x, y);
    Text(rp, (CONST_STRPTR)str, strlen(str));
}

void draw_number(struct RastPort *rp, WORD x, WORD y, LONG num)
{
    char buf[12];
    WORD len = 0;
    LONG n = num;
    WORD i;

    if (n < 0) { buf[len++] = '-'; n = -n; }
    if (n == 0) { buf[len++] = '0'; }
    else {
        char tmp[11];
        WORD tl = 0;
        while (n > 0) { tmp[tl++] = '0' + (char)(n % 10); n /= 10; }
        for (i = tl - 1; i >= 0; i--) buf[len++] = tmp[i];
    }
    buf[len] = 0;
    Move(rp, x, y);
    Text(rp, (CONST_STRPTR)buf, len);
}

/* Draw RJ */
void draw_player(struct RastPort *rp, GameState *gs)
{
    WORD sx = W2S(gs, gs->player.x);
    WORD sy = gs->player.y;
    WORD f = gs->player.frame;

    if (sx < -PLAYER_W || sx >= SCREEN_W) return;

    /* Party hat (triangle) */
    SetAPen(rp, COL_RED);
    Move(rp, sx + 6, sy - PLAYER_H);
    Draw(rp, sx + 2, sy - PLAYER_H + 8);
    Draw(rp, sx + 10, sy - PLAYER_H + 8);
    Draw(rp, sx + 6, sy - PLAYER_H);
    /* Hat pom-pom */
    SetAPen(rp, COL_BTYELLOW);
    RectFill(rp, sx + 5, sy - PLAYER_H - 2, sx + 7, sy - PLAYER_H);

    /* Head */
    SetAPen(rp, COL_TAN);
    RectFill(rp, sx + 3, sy - PLAYER_H + 8, sx + 9, sy - PLAYER_H + 15);

    /* Glasses */
    SetAPen(rp, COL_BLUE);
    RectFill(rp, sx + 3, sy - PLAYER_H + 10, sx + 5, sy - PLAYER_H + 12);
    RectFill(rp, sx + 7, sy - PLAYER_H + 10, sx + 9, sy - PLAYER_H + 12);
    /* Bridge */
    Move(rp, sx + 5, sy - PLAYER_H + 11);
    Draw(rp, sx + 7, sy - PLAYER_H + 11);

    /* Body */
    SetAPen(rp, COL_LTBLUE);
    RectFill(rp, sx + 4, sy - PLAYER_H + 16, sx + 8, sy - 6);

    /* Legs - animate */
    SetAPen(rp, COL_BLUE);
    if (f & 1) {
        /* Walking frame 1 */
        RectFill(rp, sx + 4, sy - 5, sx + 5, sy);
        RectFill(rp, sx + 7, sy - 5, sx + 8, sy);
    } else {
        /* Walking frame 2 / standing */
        RectFill(rp, sx + 3, sy - 5, sx + 4, sy);
        RectFill(rp, sx + 8, sy - 5, sx + 9, sy);
    }

    /* Carrying item indicator */
    if (gs->player.carrying != ITEM_NONE) {
        WORD ic;
        switch (gs->player.carrying) {
            case ITEM_GIFT:    ic = COL_RED; break;
            case ITEM_BROWNIE: ic = COL_DKBROWN; break;
            case ITEM_SUSHI_R: ic = COL_RED; break;
            case ITEM_SUSHI_B: ic = COL_BLUE; break;
            case ITEM_SUSHI_G: ic = COL_GREEN; break;
            case ITEM_FAVOR:   ic = COL_YELLOW; break;
            case ITEM_CAKE:    ic = COL_PINK; break;
            default:           ic = COL_WHITE; break;
        }
        SetAPen(rp, ic);
        RectFill(rp, sx + 10, sy - PLAYER_H + 14, sx + 14, sy - PLAYER_H + 18);
    }

    /* RJ speech bubble */
    if (gs->rj_bubble_timer > 0 && gs->rj_bubble[0]) {
        WORD tlen = (WORD)strlen(gs->rj_bubble);
        WORD bw = tlen * 8 + 4;
        WORD bx = sx + 6 - bw / 2;
        WORD by = sy - PLAYER_H - 18;

        if (bx < 1) bx = 1;
        if (bx + bw >= SCREEN_W) bx = SCREEN_W - bw - 1;
        if (by >= HUD_H) {
            /* Bubble background */
            SetAPen(rp, COL_BTYELLOW);
            RectFill(rp, bx, by, bx + bw, by + 10);
            /* Tail */
            Move(rp, bx + bw / 2, by + 11);
            Draw(rp, bx + bw / 2 + 2, by + 13);
            /* Text */
            SetAPen(rp, COL_RED);
            Move(rp, bx + 2, by + 8);
            Text(rp, (CONST_STRPTR)gs->rj_bubble, tlen);
        }
    }

    /* Arcade play effect - screen flash around RJ */
    if (gs->arcade_timer > 0 && (gs->arcade_timer & 4)) {
        WORD ec = gs->arcade_which == 0 ? COL_RED : COL_ORANGE;
        SetAPen(rp, ec);
        /* Sparks around RJ */
        RectFill(rp, sx - 3, sy - PLAYER_H - 2, sx - 1, sy - PLAYER_H);
        RectFill(rp, sx + 13, sy - PLAYER_H - 2, sx + 15, sy - PLAYER_H);
        RectFill(rp, sx - 3, sy - 4, sx - 1, sy - 2);
        RectFill(rp, sx + 13, sy - 4, sx + 15, sy - 2);
    }
}

/* Draw a single guest */
static void draw_one_guest(struct RastPort *rp, GameState *gs, Guest *g)
{
    WORD sx = W2S(gs, g->x);
    WORD sy = g->y;
    WORD col = g->color;

    if (sx < -GUEST_W || sx >= SCREEN_W) return;

    /* Head */
    SetAPen(rp, COL_TAN);
    RectFill(rp, sx + 2, sy - GUEST_H, sx + 8, sy - GUEST_H + 6);

    /* Hair/hat in guest color */
    SetAPen(rp, col);
    RectFill(rp, sx + 1, sy - GUEST_H - 1, sx + 9, sy - GUEST_H + 1);

    /* Body in guest color */
    SetAPen(rp, col);
    RectFill(rp, sx + 3, sy - GUEST_H + 7, sx + 7, sy - 3);

    /* Legs */
    SetAPen(rp, COL_BLUE);
    RectFill(rp, sx + 3, sy - 2, sx + 4, sy);
    RectFill(rp, sx + 6, sy - 2, sx + 7, sy);

    /* Speech bubble - greeting text */
    if (g->bubble_timer > 0 && g->bubble_text[0]) {
        WORD tlen = (WORD)strlen(g->bubble_text);
        WORD bw = tlen * 8 + 4;
        WORD bx = sx + 5 - bw / 2;
        WORD by = sy - GUEST_H - 16;

        if (bx < 1) bx = 1;
        if (bx + bw >= SCREEN_W) bx = SCREEN_W - bw - 1;
        if (by >= HUD_H && by < SCREEN_H - 10) {
            /* Bubble background */
            SetAPen(rp, COL_WHITE);
            RectFill(rp, bx, by, bx + bw, by + 10);
            /* Tail */
            Move(rp, bx + bw / 2, by + 11);
            Draw(rp, bx + bw / 2 + 2, by + 13);
            /* Text */
            SetAPen(rp, COL_BLUE);
            Move(rp, bx + 2, by + 8);
            Text(rp, (CONST_STRPTR)g->bubble_text, tlen);
        }
    }
    /* Item request bubble (when no text bubble) */
    else if (g->state == GUEST_WANT && g->bubble_timer > 0) {
        WORD bx = sx + 5;
        WORD by = sy - GUEST_H - 12;
        WORD ic;

        if (bx >= 2 && bx < SCREEN_W - 12) {
            SetAPen(rp, COL_WHITE);
            RectFill(rp, bx - 2, by, bx + 10, by + 8);
            Move(rp, bx + 3, by + 9);
            Draw(rp, bx + 5, by + 11);

            switch (g->want) {
                case ITEM_GIFT:    ic = COL_RED; break;
                case ITEM_BROWNIE: ic = COL_DKBROWN; break;
                case ITEM_SUSHI_R: ic = COL_RED; break;
                case ITEM_SUSHI_B: ic = COL_BLUE; break;
                case ITEM_SUSHI_G: ic = COL_GREEN; break;
                case ITEM_FAVOR:   ic = COL_YELLOW; break;
                case ITEM_DEMO:    ic = COL_LTBLUE; break;
                default:           ic = COL_GREY; break;
            }
            SetAPen(rp, ic);
            RectFill(rp, bx + 1, by + 1, bx + 7, by + 7);
        }
    }

    /* Patience bar */
    if (g->state == GUEST_WANT && g->patience_max > 0) {
        WORD bar_w = 10;
        WORD filled = (g->patience * bar_w) / g->patience_max;
        WORD bx = sx;
        WORD by = sy - GUEST_H - 3;

        if (bx >= 0 && bx + bar_w < SCREEN_W) {
            SetAPen(rp, COL_RED);
            RectFill(rp, bx, by, bx + bar_w, by + 1);
            SetAPen(rp, COL_GREEN);
            if (filled > 0)
                RectFill(rp, bx, by, bx + filled, by + 1);
        }
    }

    /* Name tag */
    if (g->name_idx >= 0 && g->name_idx < gs->name_count) {
        WORD nx = sx - 2;
        WORD ny = sy + 6;
        WORD nlen = (WORD)strlen(gs->names[g->name_idx]);
        if (nx >= 0 && nx + nlen * 8 < SCREEN_W && ny < SCREEN_H) {
            SetAPen(rp, COL_WHITE);
            Move(rp, nx, ny);
            Text(rp, (CONST_STRPTR)gs->names[g->name_idx], nlen);
        }
    }

    /* Happy face */
    if (g->state == GUEST_HAPPY) {
        SetAPen(rp, COL_BTYELLOW);
        Move(rp, sx + 3, sy - GUEST_H + 4);
        Draw(rp, sx + 4, sy - GUEST_H + 5);
        Draw(rp, sx + 6, sy - GUEST_H + 5);
        Draw(rp, sx + 7, sy - GUEST_H + 4);
    }

    /* Angry indicator */
    if (g->state == GUEST_ANGRY) {
        SetAPen(rp, COL_RED);
        Move(rp, sx + 2, sy - GUEST_H + 3);
        Draw(rp, sx + 4, sy - GUEST_H + 5);
        Move(rp, sx + 8, sy - GUEST_H + 3);
        Draw(rp, sx + 6, sy - GUEST_H + 5);
    }
}

void draw_guests(struct RastPort *rp, GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_GUESTS; i++) {
        if (gs->guests[i].active)
            draw_one_guest(rp, gs, &gs->guests[i]);
    }
}

/* Draw items */
void draw_items(struct RastPort *rp, GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_ITEMS; i++) {
        Item *it = &gs->items[i];
        WORD sx, sy, col;

        if (!it->active) continue;
        sx = W2S(gs, it->x);
        sy = it->y;
        if (sx < -10 || sx >= SCREEN_W) continue;

        switch (it->type) {
            case ITEM_GIFT:
                /* Gift box with bow */
                SetAPen(rp, COL_RED);
                RectFill(rp, sx, sy - 8, sx + 8, sy);
                SetAPen(rp, COL_BTYELLOW);
                Move(rp, sx + 4, sy - 8);
                Draw(rp, sx + 4, sy);
                Move(rp, sx, sy - 4);
                Draw(rp, sx + 8, sy - 4);
                /* Bow */
                SetAPen(rp, COL_YELLOW);
                RectFill(rp, sx + 2, sy - 10, sx + 6, sy - 8);
                break;
            case ITEM_BROWNIE:
                SetAPen(rp, COL_DKBROWN);
                RectFill(rp, sx, sy - 4, sx + 8, sy);
                SetAPen(rp, COL_BROWN);
                RectFill(rp, sx + 1, sy - 3, sx + 7, sy - 1);
                break;
            case ITEM_SUSHI_R:
                col = COL_RED;
                goto draw_sushi;
            case ITEM_SUSHI_B:
                col = COL_BLUE;
                goto draw_sushi;
            case ITEM_SUSHI_G:
                col = COL_GREEN;
            draw_sushi:
                SetAPen(rp, COL_WHITE);
                RectFill(rp, sx, sy - 5, sx + 7, sy);
                SetAPen(rp, col);
                RectFill(rp, sx + 1, sy - 4, sx + 6, sy - 1);
                break;
            case ITEM_FAVOR:
                /* Diamond shape */
                SetAPen(rp, COL_YELLOW);
                Move(rp, sx + 3, sy - 6);
                Draw(rp, sx + 6, sy - 3);
                Draw(rp, sx + 3, sy);
                Draw(rp, sx, sy - 3);
                Draw(rp, sx + 3, sy - 6);
                break;
            case ITEM_CAKE:
                /* Slice of cake */
                SetAPen(rp, COL_WHITE);
                RectFill(rp, sx, sy - 6, sx + 7, sy);
                SetAPen(rp, COL_PINK);
                RectFill(rp, sx, sy - 7, sx + 7, sy - 6);
                /* Cherry on top */
                SetAPen(rp, COL_RED);
                RectFill(rp, sx + 3, sy - 9, sx + 5, sy - 7);
                break;
            case ITEM_PLANT:
                /* Cannabis leaf - green star shape */
                SetAPen(rp, COL_GREEN);
                RectFill(rp, sx + 2, sy - 8, sx + 5, sy);  /* stem */
                /* Leaves */
                RectFill(rp, sx, sy - 7, sx + 7, sy - 5);
                RectFill(rp, sx + 1, sy - 9, sx + 6, sy - 3);
                /* Highlight */
                SetAPen(rp, COL_LTGREEN);
                RectFill(rp, sx + 2, sy - 7, sx + 5, sy - 5);
                break;
            default:
                SetAPen(rp, COL_WHITE);
                RectFill(rp, sx, sy - 4, sx + 6, sy);
                break;
        }
    }
}

/* Smoke puffs */
void draw_puffs(struct RastPort *rp, GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_PUFFS; i++) {
        SmokePuff *p = &gs->puffs[i];
        WORD sx, sy, s;
        if (p->life <= 0) continue;
        sx = p->x - gs->camera_x;
        sy = p->y;
        if (sx < -10 || sx >= SCREEN_W + 10) continue;
        s = p->size;

        /* Fade color based on life */
        if (p->life > PUFF_LIFE / 2)
            SetAPen(rp, COL_WHITE);
        else if (p->life > PUFF_LIFE / 4)
            SetAPen(rp, COL_GREY);
        else
            SetAPen(rp, COL_LTGREEN);

        /* Draw puff as a filled rectangle (approximating a cloud) */
        if (sy - s >= HUD_H && sy + s < SCREEN_H)
            RectFill(rp, sx - s, sy - s, sx + s, sy + s);
    }
}

/* Boing balls - true checkerboard: horizontal bands x vertical stripes */
void draw_boings(struct RastPort *rp, GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_BOINGS; i++) {
        BoingBall *b = &gs->boings[i];
        WORD sx, sy, r, dy;
        if (!b->active) continue;

        sx = b->x - gs->camera_x;
        sy = b->y;
        r = BOING_SIZE;
        if (sx < -r || sx >= SCREEN_W + r) continue;
        if (sy < HUD_H || sy >= SCREEN_H) continue;

        for (dy = -r; dy <= r; dy++) {
            WORD row_y = sy + dy;
            WORD dx_sq, half_w;
            WORD x1, x2, hband;
            WORD stripe_w = 3;  /* width of each vertical stripe */
            WORD phase = b->rot >> 2;  /* horizontal rotation */
            WORD seg_start, seg_col;

            if (row_y < HUD_H || row_y >= SCREEN_H) continue;

            dx_sq = r * r - dy * dy;
            half_w = 0;
            while ((half_w + 1) * (half_w + 1) <= dx_sq) half_w++;
            if (half_w <= 0) continue;

            x1 = sx - half_w;
            x2 = sx + half_w;
            if (x1 < 0) x1 = 0;
            if (x2 >= SCREEN_W) x2 = SCREEN_W - 1;
            if (x1 > x2) continue;

            /* Horizontal band (rows): alternates every 3 pixels */
            hband = ((dy + phase) / 3) & 1;

            /* Draw vertical stripes across this row */
            seg_start = x1;
            seg_col = (((x1 - sx + phase) / stripe_w) & 1) ^ hband;

            {
                WORD px;
                for (px = x1; px <= x2 + 1; px++) {
                    WORD col;
                    if (px <= x2)
                        col = (((px - sx + phase) / stripe_w) & 1) ^ hband;
                    else
                        col = -1;

                    if (col != seg_col) {
                        if (seg_col >= 0) {
                            WORD end = px - 1;
                            if (end > x2) end = x2;
                            SetAPen(rp, seg_col ? COL_RED : COL_WHITE);
                            RectFill(rp, seg_start, row_y, end, row_y);
                        }
                        seg_start = px;
                        seg_col = col;
                    }
                }
            }
        }

        /* Shadow */
        {
            WORD shx1 = sx - r / 2;
            WORD shx2 = sx + r / 2;
            if (shx1 < 0) shx1 = 0;
            if (shx2 >= SCREEN_W) shx2 = SCREEN_W - 1;
            if (FLOOR_Y + 1 < SCREEN_H && shx1 <= shx2) {
                SetAPen(rp, COL_DKRED);
                RectFill(rp, shx1, FLOOR_Y + 1, shx2, FLOOR_Y + 2);
            }
        }
    }
}

/* Pot Police */
void draw_cops(struct RastPort *rp, GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_COPS; i++) {
        PotCop *c = &gs->cops[i];
        WORD sx, sy;
        if (!c->active) continue;

        sx = c->x - gs->camera_x;
        sy = c->y;
        if (sx < -COP_W || sx >= SCREEN_W + COP_W) continue;

        /* Flash when on cooldown (just hit player) */
        if (c->cooldown > 0 && (c->cooldown & 4)) continue;

        /* Hat - blue police cap */
        SetAPen(rp, COL_BLUE);
        RectFill(rp, sx + 1, sy - COP_H, sx + 9, sy - COP_H + 3);
        /* Hat brim */
        SetAPen(rp, COL_BLUE);
        RectFill(rp, sx, sy - COP_H + 3, sx + 10, sy - COP_H + 4);

        /* Badge on hat */
        SetAPen(rp, COL_BTYELLOW);
        RectFill(rp, sx + 4, sy - COP_H + 1, sx + 6, sy - COP_H + 2);

        /* Head */
        SetAPen(rp, COL_TAN);
        RectFill(rp, sx + 2, sy - COP_H + 5, sx + 8, sy - COP_H + 10);

        /* Angry eyes */
        SetAPen(rp, COL_RED);
        RectFill(rp, sx + 3, sy - COP_H + 7, sx + 4, sy - COP_H + 8);
        RectFill(rp, sx + 6, sy - COP_H + 7, sx + 7, sy - COP_H + 8);

        /* Body - dark blue uniform */
        SetAPen(rp, COL_BLUE);
        RectFill(rp, sx + 3, sy - COP_H + 11, sx + 7, sy - 5);

        /* Badge on chest */
        SetAPen(rp, COL_BTYELLOW);
        RectFill(rp, sx + 4, sy - COP_H + 12, sx + 6, sy - COP_H + 14);

        /* Legs - animate */
        SetAPen(rp, COL_BG);
        if (c->frame & 1) {
            SetAPen(rp, COL_BLUE);
            RectFill(rp, sx + 3, sy - 4, sx + 4, sy);
            RectFill(rp, sx + 6, sy - 4, sx + 7, sy);
        } else {
            SetAPen(rp, COL_BLUE);
            RectFill(rp, sx + 2, sy - 4, sx + 3, sy);
            RectFill(rp, sx + 7, sy - 4, sx + 8, sy);
        }

        /* "!" when chasing */
        if (c->chase) {
            SetAPen(rp, COL_RED);
            RectFill(rp, sx + 4, sy - COP_H - 6, sx + 6, sy - COP_H - 2);
            RectFill(rp, sx + 4, sy - COP_H - 1, sx + 6, sy - COP_H);
        }
    }
}

/* HUD */
void draw_hud(struct RastPort *rp, GameState *gs)
{
    WORD i;
    WORD hour;
    char time_str[8];

    /* Background bar */
    SetAPen(rp, COL_DKBROWN);
    RectFill(rp, 0, 0, SCREEN_W - 1, HUD_H - 1);

    /* Score */
    SetAPen(rp, COL_BTYELLOW);
    draw_text(rp, 4, 10, "SCORE:");
    draw_number(rp, 56, 10, gs->score);

    /* Lives (party hats) */
    for (i = 0; i < gs->lives && i < 5; i++) {
        WORD lx = 140 + i * 14;
        SetAPen(rp, COL_RED);
        Move(rp, lx + 4, 2);
        Draw(rp, lx, 12);
        Draw(rp, lx + 8, 12);
        Draw(rp, lx + 4, 2);
    }

    /* Party clock */
    hour = 7 + (gs->party_clock * 5) / PARTY_FRAMES;
    if (hour > 12) hour = 12;
    time_str[0] = (hour >= 10) ? '1' : ' ';
    time_str[1] = '0' + (hour % 10);
    time_str[2] = ':';
    time_str[3] = '0';
    time_str[4] = '0';
    time_str[5] = 0;
    /* Add minutes based on sub-hour position */
    {
        WORD sub = (gs->party_clock * 5) % PARTY_FRAMES;
        WORD mins = (sub * 60) / PARTY_FRAMES;
        time_str[3] = '0' + (mins / 10);
        time_str[4] = '0' + (mins % 10);
    }
    SetAPen(rp, COL_WHITE);
    draw_text(rp, 260, 10, time_str);

    /* Room name at bottom */
    if (gs->current_room >= 0 && gs->current_room < ROOM_COUNT) {
        const char *rn;
        WORD rl;
        extern const char *room_name(WORD room);
        rn = room_name(gs->current_room);
        rl = (WORD)strlen(rn);
        {
            WORD rw = rl * 8 + 8;
            WORD rx = (SCREEN_W - rw) / 2;
            SetAPen(rp, COL_BG);
            RectFill(rp, rx, SCREEN_H - 12, rx + rw, SCREEN_H - 1);
            SetAPen(rp, COL_BTYELLOW);
            draw_text(rp, rx + 4, SCREEN_H - 4, rn);
        }
    }

    /* Message - centered with background box */
    if (gs->msg_timer > 0) {
        WORD ml = (WORD)strlen(gs->msg);
        WORD mw = ml * 8 + 8;
        WORD mx = (SCREEN_W - mw) / 2;
        WORD my = WALK_Y_MIN - 15;
        SetAPen(rp, COL_BG);
        RectFill(rp, mx - 2, my - 10, mx + mw + 2, my + 2);
        SetAPen(rp, COL_DKBROWN);
        RectFill(rp, mx - 2, my - 10, mx + mw + 2, my - 10);
        RectFill(rp, mx - 2, my + 2, mx + mw + 2, my + 2);
        SetAPen(rp, (gs->msg_timer & 4) ? COL_BTYELLOW : COL_WHITE);
        draw_text(rp, mx + 4, my - 1, gs->msg);
    }

    /* Easter egg text - bottom area with background box */
    if (gs->egg_timer > 0) {
        WORD el = (WORD)strlen(gs->egg_text);
        WORD ew = el * 8 + 8;
        WORD ex = (SCREEN_W - ew) / 2;
        WORD ey = SCREEN_H - 28;
        /* Background box */
        SetAPen(rp, COL_DKBROWN);
        RectFill(rp, ex - 2, ey - 10, ex + ew + 2, ey + 2);
        SetAPen(rp, COL_BG);
        RectFill(rp, ex, ey - 8, ex + ew, ey);
        /* Text */
        SetAPen(rp, COL_BTYELLOW);
        draw_text(rp, ex + 4, ey - 1, gs->egg_text);
    }
}

/* Title screen */
void draw_title(struct RastPort *rp, GameState *gs)
{
    SetRast(rp, COL_BG);

    /* Border */
    SetAPen(rp, COL_BTYELLOW);
    RectFill(rp, 30, 30, 290, 32);
    RectFill(rp, 30, 30, 32, 220);
    RectFill(rp, 30, 218, 290, 220);
    RectFill(rp, 288, 30, 290, 220);

    /* Title */
    SetAPen(rp, COL_RED);
    draw_text(rp, 60, 60, "RJ'S 70TH BIRTHDAY");
    SetAPen(rp, COL_BTYELLOW);
    draw_text(rp, 116, 80, "BASH!");

    /* RJ portrait - bitmap head on a drawn body */
    /* Party hat on top of head */
    SetAPen(rp, COL_RED);
    Move(rp, 160, 82);
    Draw(rp, 148, 92);
    Draw(rp, 172, 92);
    Draw(rp, 160, 82);
    /* Pom-pom */
    SetAPen(rp, COL_BTYELLOW);
    RectFill(rp, 158, 79, 162, 82);
    /* Bitmap head */
    draw_rj_head(rp, 144, 90);
    /* Body (purple shirt like the photo) */
    SetAPen(rp, COL_PINK);
    RectFill(rp, 148, 130, 172, 160);
    /* Collar */
    SetAPen(rp, COL_WHITE);
    RectFill(rp, 155, 130, 158, 135);
    RectFill(rp, 162, 130, 165, 135);
    /* Arms */
    SetAPen(rp, COL_PINK);
    RectFill(rp, 140, 133, 148, 150);
    RectFill(rp, 172, 133, 180, 150);
    /* Hands */
    SetAPen(rp, COL_TAN);
    RectFill(rp, 140, 150, 148, 155);
    RectFill(rp, 172, 150, 180, 155);

    /* Instructions */
    SetAPen(rp, COL_WHITE);
    draw_text(rp, 44, 175, "KEEP THE PARTY GOING!");
    draw_text(rp, 56, 190, "ARROWS/JOYSTICK+FIRE");

    /* Blink prompt */
    if (gs->title_blink & 16) {
        SetAPen(rp, COL_YELLOW);
        draw_text(rp, 60, 206, "PRESS FIRE TO PARTY!");
    }

    /* Menu options */
    SetAPen(rp, COL_LTGREEN);
    draw_text(rp, 36, 222, "H=HELP E=GUESTS Q=QUIT");

    /* Tribute */
    SetAPen(rp, COL_GREY);
    draw_text(rp, 48, 238, "A TRIBUTE TO RJ MICAL");
    SetAPen(rp, COL_DKBROWN);
    draw_text(rp, 20, 252, "chris@hitorro.com 2026");
}

/* Credits screen */
void draw_credits(struct RastPort *rp, GameState *gs)
{
    WORD y = 256 - gs->credits_scroll;
    WORD i;

    SetRast(rp, COL_BG);

    SetAPen(rp, COL_BTYELLOW);
    if (y > 0 && y < 260) draw_text(rp, 52, y, "HAPPY 70TH BIRTHDAY");
    y += 16;
    if (y > 0 && y < 260) draw_text(rp, 116, y, "RJ!");
    y += 30;

    SetAPen(rp, COL_WHITE);
    if (y > 0 && y < 260) draw_text(rp, 72, y, "FROM YOUR FRIENDS:");
    y += 20;

    SetAPen(rp, COL_YELLOW);
    for (i = 0; i < gs->name_count; i++) {
        WORD nl = (WORD)strlen(gs->names[i]);
        WORD nx = (SCREEN_W - nl * 8) / 2;
        if (y > 0 && y < 260)
            draw_text(rp, nx, y, gs->names[i]);
        y += 14;
    }

    y += 20;
    SetAPen(rp, COL_LTBLUE);
    if (y > 0 && y < 260) draw_text(rp, 36, y, "THANK YOU FOR THE AMIGA");
    y += 14;
    if (y > 0 && y < 260) draw_text(rp, 52, y, "THE LYNX, THE 3DO,");
    y += 14;
    if (y > 0 && y < 260) draw_text(rp, 28, y, "AND EVERYTHING IN BETWEEN.");
    y += 24;

    SetAPen(rp, COL_BTYELLOW);
    if (y > 0 && y < 260) draw_text(rp, 44, y, "YOU CHANGED THE WORLD.");
    y += 30;

    SetAPen(rp, COL_GREY);
    if (y > 0 && y < 260) draw_text(rp, 20, y, "chris@hitorro.com 2026");
    y += 20;

    SetAPen(rp, COL_GREY);
    if (y > 0 && y < 260) draw_text(rp, 80, y, "PRESS FIRE TO PLAY");
}

/* Game over */
void draw_gameover(struct RastPort *rp, GameState *gs)
{
    SetRast(rp, COL_BG);

    SetAPen(rp, COL_RED);
    draw_text(rp, 76, 80, "PARTY'S OVER, MAN!");

    SetAPen(rp, COL_WHITE);
    draw_text(rp, 108, 110, "FINAL SCORE:");
    SetAPen(rp, COL_BTYELLOW);
    draw_number(rp, 120, 130, gs->score);

    SetAPen(rp, COL_GREY);
    draw_text(rp, 60, 180, "PRESS FIRE TO CONTINUE");
}

/* Win screen */
void draw_win(struct RastPort *rp, GameState *gs)
{
    SetRast(rp, COL_BG);

    SetAPen(rp, COL_BTYELLOW);
    draw_text(rp, 56, 60, "WHAT A PARTY!!!");

    SetAPen(rp, COL_WHITE);
    draw_text(rp, 108, 90, "FINAL SCORE:");
    SetAPen(rp, COL_YELLOW);
    draw_number(rp, 120, 110, gs->score);

    /* Cake */
    SetAPen(rp, COL_WHITE);
    RectFill(rp, 120, 130, 200, 170);
    SetAPen(rp, COL_PINK);
    RectFill(rp, 122, 128, 198, 132);
    RectFill(rp, 122, 148, 198, 152);
    SetAPen(rp, COL_RED);
    draw_text(rp, 148, 163, "70");
    /* Candles */
    SetAPen(rp, COL_BTYELLOW);
    RectFill(rp, 140, 118, 143, 128);
    RectFill(rp, 158, 118, 161, 128);
    RectFill(rp, 176, 118, 179, 128);
    SetAPen(rp, COL_ORANGE);
    RectFill(rp, 140, 114, 143, 118);
    RectFill(rp, 158, 114, 161, 118);
    RectFill(rp, 176, 114, 179, 118);

    SetAPen(rp, COL_BTYELLOW);
    draw_text(rp, 36, 200, "HAPPY BIRTHDAY RJ MICAL!");

    SetAPen(rp, COL_GREY);
    draw_text(rp, 60, 230, "PRESS FIRE FOR CREDITS");
}

/* High score table */
void draw_hiscore(struct RastPort *rp, GameState *gs)
{
    WORD i, y;

    SetRast(rp, COL_BG);

    SetAPen(rp, COL_BTYELLOW);
    draw_text(rp, 76, 40, "HALL OF PARTY!");

    SetAPen(rp, COL_ORANGE);
    draw_text(rp, 36, 60, "RANK  NAME         SCORE");
    SetAPen(rp, COL_GREY);
    RectFill(rp, 36, 63, 280, 63);

    y = 80;
    for (i = 0; i < gs->hi_count && i < MAX_HISCORES; i++) {
        char rank[4];
        rank[0] = '1' + (char)i;
        rank[1] = '.';
        rank[2] = 0;

        SetAPen(rp, COL_WHITE);
        draw_text(rp, 44, y, rank);

        SetAPen(rp, (i == 0) ? COL_BTYELLOW : COL_WHITE);
        draw_text(rp, 76, y, gs->hi_names[i]);

        SetAPen(rp, COL_YELLOW);
        draw_number(rp, 220, y, gs->hi_scores[i]);

        y += 18;
    }

    SetAPen(rp, COL_LTGREEN);
    draw_text(rp, 20, 190, "FIRE: ADD YOUR NAME TO");
    draw_text(rp, 44, 205, "THE GUEST LIST!");

    SetAPen(rp, COL_GREY);
    draw_text(rp, 52, 235, "PRESS FIRE TO CONTINUE");
}

/* Name entry screen */
void draw_enter_name(struct RastPort *rp, GameState *gs)
{
    WORD cx;

    SetRast(rp, COL_BG);

    if (gs->entry_mode == 0) {
        /* High score entry */
        SetAPen(rp, COL_BTYELLOW);
        draw_text(rp, 60, 50, "NEW HIGH SCORE!");
        SetAPen(rp, COL_YELLOW);
        draw_number(rp, 120, 75, gs->score);
    } else {
        /* Add guest */
        SetAPen(rp, COL_LTGREEN);
        draw_text(rp, 28, 50, "JOIN THE GUEST LIST!");
        SetAPen(rp, COL_WHITE);
        draw_text(rp, 20, 75, "TYPE YOUR NAME TO ATTEND");
        draw_text(rp, 36, 90, "RJ'S BIRTHDAY PARTY!");
    }

    SetAPen(rp, COL_WHITE);
    draw_text(rp, 52, 120, "ENTER YOUR NAME:");

    /* Name entry box */
    SetAPen(rp, COL_DKBROWN);
    RectFill(rp, 60, 135, 260, 155);
    SetAPen(rp, COL_BROWN);
    RectFill(rp, 62, 137, 258, 153);

    /* Typed name */
    if (gs->entry_pos > 0) {
        SetAPen(rp, COL_BTYELLOW);
        draw_text(rp, 68, 149, gs->entry_name);
    }

    /* Blinking cursor */
    cx = 68 + gs->entry_pos * 8;
    if (gs->entry_blink & 8) {
        SetAPen(rp, COL_WHITE);
        RectFill(rp, cx, 140, cx + 6, 152);
    }

    SetAPen(rp, COL_GREY);
    draw_text(rp, 44, 180, "TYPE + RETURN TO CONFIRM");
    draw_text(rp, 68, 200, "ESC TO SKIP");
}

/* Help screen */
void draw_help(struct RastPort *rp, GameState *gs)
{
    WORD y = 28;
    (void)gs;

    SetRast(rp, COL_BG);

    SetAPen(rp, COL_BTYELLOW);
    draw_text(rp, 76, y, "HOW TO PARTY!");
    y += 20;

    SetAPen(rp, COL_WHITE);
    draw_text(rp, 12, y, "YOU ARE RJ MICAL. IT'S YOUR");
    y += 12;
    draw_text(rp, 12, y, "70TH BIRTHDAY! KEEP GUESTS");
    y += 12;
    draw_text(rp, 12, y, "HAPPY ACROSS 6 THEMED ROOMS.");
    y += 18;

    SetAPen(rp, COL_YELLOW);
    draw_text(rp, 12, y, "CONTROLS:");
    y += 12;
    SetAPen(rp, COL_WHITE);
    draw_text(rp, 12, y, "ARROWS/JOYSTICK = MOVE");
    y += 11;
    draw_text(rp, 12, y, "SPACE/FIRE = INTERACT");
    y += 11;
    draw_text(rp, 12, y, "ESC = QUIT");
    y += 16;

    SetAPen(rp, COL_YELLOW);
    draw_text(rp, 12, y, "ROOMS:");
    y += 12;
    SetAPen(rp, COL_ORANGE);
    draw_text(rp, 12, y, "FOYER");
    SetAPen(rp, COL_GREY);
    draw_text(rp, 60, y, "- CATCH GIFTS!");
    y += 11;
    SetAPen(rp, COL_GREEN);
    draw_text(rp, 12, y, "AMSTERDAM");
    SetAPen(rp, COL_GREY);
    draw_text(rp, 92, y, "- SERVE+COLLECT");
    y += 11;
    SetAPen(rp, COL_RED);
    draw_text(rp, 12, y, "TOKYO");
    SetAPen(rp, COL_GREY);
    draw_text(rp, 60, y, "- MATCH SUSHI");
    y += 11;
    SetAPen(rp, COL_LTBLUE);
    draw_text(rp, 12, y, "SILICON V");
    SetAPen(rp, COL_GREY);
    draw_text(rp, 84, y, "- DEMO+ARCADES");
    y += 11;
    SetAPen(rp, COL_YELLOW);
    draw_text(rp, 12, y, "BAY AREA");
    SetAPen(rp, COL_GREY);
    draw_text(rp, 76, y, "- DODGE+DELIVER");
    y += 11;
    SetAPen(rp, COL_PINK);
    draw_text(rp, 12, y, "LIVING RM");
    SetAPen(rp, COL_GREY);
    draw_text(rp, 84, y, "- BIRTHDAY CAKE");
    y += 16;

    SetAPen(rp, COL_LTGREEN);
    draw_text(rp, 12, y, "GREET FRIENDS WITH FIRE!");
    y += 14;

    SetAPen(rp, COL_GREY);
    draw_text(rp, 44, 248, "PRESS FIRE TO GO BACK");
}

/* Guest list editor */
void draw_guest_edit(struct RastPort *rp, GameState *gs)
{
    WORD i, y;
    WORD max_visible = 10;

    SetRast(rp, COL_BG);

    SetAPen(rp, COL_BTYELLOW);
    draw_text(rp, 60, 30, "GUEST LIST EDITOR");

    SetAPen(rp, COL_GREY);
    RectFill(rp, 40, 35, 280, 35);

    y = 50;
    for (i = 0; i < gs->name_count && i < max_visible; i++) {
        WORD selected = (i == gs->edit_cursor);

        if (selected) {
            /* Highlight bar */
            SetAPen(rp, COL_DKBROWN);
            RectFill(rp, 42, y - 9, 278, y + 2);
        }

        SetAPen(rp, selected ? COL_BTYELLOW : COL_WHITE);
        draw_text(rp, 50, y, gs->names[i]);

        y += 16;
    }

    /* "Add new" slot at bottom */
    if (gs->edit_cursor >= gs->name_count) {
        SetAPen(rp, COL_DKBROWN);
        RectFill(rp, 42, y - 9, 278, y + 2);
    }
    SetAPen(rp, (gs->edit_cursor >= gs->name_count) ? COL_LTGREEN : COL_GREEN);
    draw_text(rp, 50, y, "+ ADD NEW GUEST");

    /* Instructions */
    y = 220;
    SetAPen(rp, COL_GREY);
    draw_text(rp, 28, y, "UP/DOWN = SELECT");
    y += 12;
    draw_text(rp, 28, y, "DEL = REMOVE  RET = ADD");
    y += 12;
    draw_text(rp, 28, y, "ESC = SAVE AND EXIT");
}

/* Jail screen */
void draw_jail(struct RastPort *rp, GameState *gs)
{
    WORD bx, by, i;
    WORD secs;
    char countdown[4];

    SetRast(rp, COL_BG);

    /* BUSTED! header */
    SetAPen(rp, COL_RED);
    draw_text(rp, 108, 30, "BUSTED!");

    /* Jail cell - brick walls */
    SetAPen(rp, COL_GREY);
    RectFill(rp, 60, 50, 260, 200);
    SetAPen(rp, COL_DKBROWN);
    RectFill(rp, 65, 55, 255, 195);

    /* Bricks pattern */
    SetAPen(rp, COL_GREY);
    for (by = 55; by < 195; by += 14) {
        WORD offset = ((by / 14) & 1) ? 20 : 0;
        Move(rp, 65, by);
        Draw(rp, 255, by);
        for (bx = 65 + offset; bx < 255; bx += 40) {
            Move(rp, bx, by);
            Draw(rp, bx, by + 14);
        }
    }

    /* Bars */
    SetAPen(rp, COL_WHITE);
    for (i = 0; i < 7; i++) {
        WORD barx = 85 + i * 25;
        RectFill(rp, barx, 55, barx + 2, 195);
    }

    /* RJ behind bars */
    {
        WORD rx = 150, ry = 150;
        /* Party hat */
        SetAPen(rp, COL_RED);
        Move(rp, rx + 6, ry - 24);
        Draw(rp, rx + 2, ry - 16);
        Draw(rp, rx + 10, ry - 16);
        Draw(rp, rx + 6, ry - 24);
        /* Head */
        SetAPen(rp, COL_TAN);
        RectFill(rp, rx + 3, ry - 15, rx + 9, ry - 8);
        /* Glasses */
        SetAPen(rp, COL_BLUE);
        RectFill(rp, rx + 3, ry - 13, rx + 5, ry - 11);
        RectFill(rp, rx + 7, ry - 13, rx + 9, ry - 11);
        /* Sad mouth */
        SetAPen(rp, COL_RED);
        Move(rp, rx + 4, ry - 9);
        Draw(rp, rx + 5, ry - 10);
        Draw(rp, rx + 7, ry - 10);
        Draw(rp, rx + 8, ry - 9);
        /* Body */
        SetAPen(rp, COL_ORANGE);  /* jail jumpsuit! */
        RectFill(rp, rx + 4, ry - 7, rx + 8, ry + 3);
    }

    /* RJ's speech bubble */
    if (gs->rj_bubble_timer > 0 && gs->rj_bubble[0]) {
        WORD tlen = (WORD)strlen(gs->rj_bubble);
        WORD bw = tlen * 8 + 4;
        WORD bxx = 160 - bw / 2;
        SetAPen(rp, COL_BTYELLOW);
        RectFill(rp, bxx, 95, bxx + bw, 107);
        SetAPen(rp, COL_RED);
        draw_text(rp, bxx + 2, 105, gs->rj_bubble);
    }

    /* Countdown */
    secs = (gs->jail_timer / 50) + 1;
    if (secs > 9) secs = 9;
    countdown[0] = '0' + (char)secs;
    countdown[1] = 0;

    SetAPen(rp, COL_BTYELLOW);
    draw_text(rp, 100, 220, "RELEASED IN ");
    draw_text(rp, 196, 220, countdown);

    /* Flashing siren effect */
    if (gs->jail_timer & 8) {
        SetAPen(rp, COL_RED);
        RectFill(rp, 40, 45, 55, 55);
        RectFill(rp, 265, 45, 280, 55);
    } else {
        SetAPen(rp, COL_BLUE);
        RectFill(rp, 40, 45, 55, 55);
        RectFill(rp, 265, 45, 280, 55);
    }

    SetAPen(rp, COL_GREY);
    draw_text(rp, 64, 245, "PARTY TIME IS TICKING!");
}

/* On-screen message overlay */
void draw_message(struct RastPort *rp, GameState *gs)
{
    /* Messages are drawn as part of draw_hud already */
    (void)rp;
    (void)gs;
}
