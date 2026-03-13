/*
 * RJ and Dale's Super Bros - Enemy System
 * Bug enemies: beetle, fly, spider, ant
 */
#include "game.h"
#include <string.h>

static int sine_table[32] = {
    0, 3, 6, 9, 12, 14, 15, 16, 16, 16, 15, 14, 12, 9, 6, 3,
    0, -3, -6, -9, -12, -14, -15, -16, -16, -16, -15, -14, -12, -9, -6, -3
};

void enemies_init(void) {
    memset(game.enemies, 0, sizeof(game.enemies));
}

void enemies_spawn(const EntitySpawn *spawns) {
    int i = 0;
    const EntitySpawn *s;

    for (s = spawns; s->type != ENT_NONE && i < MAX_ENEMIES; s++) {
        if (s->type >= ENT_BEETLE && s->type <= ENT_ANT) {
            game.enemies[i].type = s->type;
            game.enemies[i].x = s->tx * TILE_SIZE;
            game.enemies[i].y = s->ty * TILE_SIZE;
            game.enemies[i].alive = 1;
            game.enemies[i].dir = DIR_LEFT;
            game.enemies[i].frame = 0;
            game.enemies[i].timer = 0;

            switch (s->type) {
            case ENT_BEETLE:
                game.enemies[i].vx = FP(1);
                game.enemies[i].vy = 0;
                break;
            case ENT_FLY:
                game.enemies[i].vx = FP(1);
                game.enemies[i].vy = 0;
                break;
            case ENT_SPIDER:
                game.enemies[i].vx = 0;
                game.enemies[i].vy = FP(1);
                game.enemies[i].y = s->ty * TILE_SIZE;
                break;
            case ENT_ANT:
                game.enemies[i].vx = FP(2);
                game.enemies[i].vy = 0;
                break;
            }
            i++;
        }
    }
}

void enemies_update(void) {
    int i;
    Enemy *e;

    for (i = 0; i < MAX_ENEMIES; i++) {
        e = &game.enemies[i];
        if (!e->alive) continue;

        e->timer++;
        e->frame = (e->timer >> 3) & 1;

        switch (e->type) {
        case ENT_BEETLE:
        case ENT_ANT:
            /* Walk left/right, reverse at walls/edges */
            e->x += e->dir * FP_INT(e->vx);

            /* Wall collision */
            if (e->dir < 0) {
                if (level_is_solid(e->x / TILE_SIZE, e->y / TILE_SIZE) ||
                    level_is_solid(e->x / TILE_SIZE, (e->y + 12) / TILE_SIZE)) {
                    e->dir = DIR_RIGHT;
                }
            } else {
                if (level_is_solid((e->x + 14) / TILE_SIZE, e->y / TILE_SIZE) ||
                    level_is_solid((e->x + 14) / TILE_SIZE, (e->y + 12) / TILE_SIZE)) {
                    e->dir = DIR_LEFT;
                }
            }

            /* Edge detection - don't walk off platforms */
            if (!level_is_solid(e->x / TILE_SIZE, (e->y + TILE_SIZE) / TILE_SIZE) &&
                !level_is_platform(e->x / TILE_SIZE, (e->y + TILE_SIZE) / TILE_SIZE) &&
                e->dir < 0) {
                e->dir = DIR_RIGHT;
            }
            if (!level_is_solid((e->x + 14) / TILE_SIZE, (e->y + TILE_SIZE) / TILE_SIZE) &&
                !level_is_platform((e->x + 14) / TILE_SIZE, (e->y + TILE_SIZE) / TILE_SIZE) &&
                e->dir > 0) {
                e->dir = DIR_LEFT;
            }
            break;

        case ENT_FLY:
            /* Fly in sine wave pattern */
            e->x += e->dir * FP_INT(e->vx);
            e->y += sine_table[e->timer & 31] / 4;

            /* Reverse at screen bounds relative to spawn */
            if (e->x < 0 || (e->x / TILE_SIZE) >= (int)level_current()->width - 1) {
                e->dir = -e->dir;
            }
            /* Simple boundary reversal */
            if (e->timer > 120) {
                e->dir = -e->dir;
                e->timer = 0;
            }
            break;

        case ENT_SPIDER:
            /* Drop down, then climb back up */
            e->y += e->dir * FP_INT(e->vy);
            if (e->dir > 0) {
                /* Dropping */
                if (level_is_solid(e->x / TILE_SIZE, (e->y + 14) / TILE_SIZE)) {
                    e->dir = DIR_LEFT; /* negative = climb up */
                }
                if (e->timer > 100) {
                    e->dir = DIR_LEFT;
                    e->timer = 0;
                }
            } else {
                /* Climbing */
                if (e->timer > 60) {
                    e->dir = DIR_RIGHT;
                    e->timer = 0;
                }
            }
            break;
        }
    }
}

void enemies_draw(struct RastPort *rp, int cam_x) {
    int i, sx, sy;
    Enemy *e;

    for (i = 0; i < MAX_ENEMIES; i++) {
        e = &game.enemies[i];
        if (!e->alive) continue;

        sx = e->x - cam_x;
        sy = e->y;

        if (sx < 0 || sx + 16 > SCREEN_W) continue;
        if (sy < 0 || sy + 16 > HUD_Y) continue;

        switch (e->type) {
        case ENT_BEETLE:
            /* Brown beetle body */
            SetAPen(rp, COL_BROWN);
            RectFill(rp, sx + 2, sy + 4, sx + 13, sy + 12);
            /* Head */
            SetAPen(rp, COL_DKBROWN);
            RectFill(rp, sx + 4, sy + 1, sx + 11, sy + 4);
            /* Legs */
            SetAPen(rp, COL_DKBROWN);
            if (e->frame) {
                RectFill(rp, sx, sy + 8, sx + 2, sy + 14);
                RectFill(rp, sx + 13, sy + 6, sx + 15, sy + 14);
            } else {
                RectFill(rp, sx, sy + 6, sx + 2, sy + 14);
                RectFill(rp, sx + 13, sy + 8, sx + 15, sy + 14);
            }
            /* Eyes */
            SetAPen(rp, COL_WHITE);
            RectFill(rp, sx + 5, sy + 2, sx + 6, sy + 3);
            RectFill(rp, sx + 9, sy + 2, sx + 10, sy + 3);
            break;

        case ENT_FLY:
            /* Grey body */
            SetAPen(rp, COL_GREY);
            RectFill(rp, sx + 4, sy + 6, sx + 11, sy + 13);
            /* Wings */
            SetAPen(rp, COL_WHITE);
            if (e->frame) {
                RectFill(rp, sx + 1, sy + 1, sx + 6, sy + 5);
                RectFill(rp, sx + 9, sy + 1, sx + 14, sy + 5);
            } else {
                RectFill(rp, sx + 1, sy + 3, sx + 6, sy + 6);
                RectFill(rp, sx + 9, sy + 3, sx + 14, sy + 6);
            }
            /* Eyes */
            SetAPen(rp, COL_RED);
            RectFill(rp, sx + 5, sy + 7, sx + 6, sy + 8);
            RectFill(rp, sx + 9, sy + 7, sx + 10, sy + 8);
            break;

        case ENT_SPIDER:
            /* Black body */
            SetAPen(rp, COL_BLACK);
            RectFill(rp, sx + 4, sy + 4, sx + 11, sy + 11);
            /* Legs extending outward */
            if (e->frame) {
                Move(rp, sx + 4, sy + 5); Draw(rp, sx, sy + 2);
                Move(rp, sx + 11, sy + 5); Draw(rp, sx + 15, sy + 2);
                Move(rp, sx + 4, sy + 9); Draw(rp, sx, sy + 13);
                Move(rp, sx + 11, sy + 9); Draw(rp, sx + 15, sy + 13);
            } else {
                Move(rp, sx + 4, sy + 5); Draw(rp, sx + 1, sy + 1);
                Move(rp, sx + 11, sy + 5); Draw(rp, sx + 14, sy + 1);
                Move(rp, sx + 4, sy + 9); Draw(rp, sx + 1, sy + 14);
                Move(rp, sx + 11, sy + 9); Draw(rp, sx + 14, sy + 14);
            }
            /* Thread from top */
            SetAPen(rp, COL_WHITE);
            Move(rp, sx + 7, sy); Draw(rp, sx + 7, sy + 4);
            /* Eyes */
            SetAPen(rp, COL_RED);
            RectFill(rp, sx + 5, sy + 6, sx + 6, sy + 7);
            RectFill(rp, sx + 9, sy + 6, sx + 10, sy + 7);
            break;

        case ENT_ANT:
            /* Small red body */
            SetAPen(rp, COL_RED);
            RectFill(rp, sx + 5, sy + 6, sx + 10, sy + 12);
            /* Head */
            RectFill(rp, sx + 6, sy + 3, sx + 9, sy + 6);
            /* Antennae */
            SetAPen(rp, COL_BLACK);
            Move(rp, sx + 6, sy + 3); Draw(rp, sx + 4, sy);
            Move(rp, sx + 9, sy + 3); Draw(rp, sx + 11, sy);
            /* Legs */
            if (e->frame) {
                Move(rp, sx + 5, sy + 8); Draw(rp, sx + 2, sy + 14);
                Move(rp, sx + 10, sy + 8); Draw(rp, sx + 13, sy + 14);
            } else {
                Move(rp, sx + 5, sy + 8); Draw(rp, sx + 3, sy + 14);
                Move(rp, sx + 10, sy + 8); Draw(rp, sx + 12, sy + 14);
            }
            break;
        }
    }
}

/* Check if player stomps an enemy (jumping on top) */
int enemies_check_stomp(int px, int py, int pw, int ph) {
    int i;
    Enemy *e;

    for (i = 0; i < MAX_ENEMIES; i++) {
        e = &game.enemies[i];
        if (!e->alive) continue;

        /* Overlap check */
        if (px + pw > e->x + 2 && px < e->x + 14 &&
            py + ph > e->y && py + ph < e->y + 10) {
            e->alive = 0;
            return 1;
        }
    }
    return 0;
}

/* Check if player touches an enemy (damage) */
int enemies_check_touch(int px, int py, int pw, int ph) {
    int i;
    Enemy *e;

    for (i = 0; i < MAX_ENEMIES; i++) {
        e = &game.enemies[i];
        if (!e->alive) continue;

        if (px + pw > e->x + 3 && px < e->x + 13 &&
            py + ph > e->y + 3 && py < e->y + 13) {
            return 1;
        }
    }
    return 0;
}
