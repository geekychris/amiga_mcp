/*
 * Jump Quest - Items System
 * Collectibles and power-ups
 */
#include "game.h"
#include <string.h>

void items_init(void) {
    memset(game.items, 0, sizeof(game.items));
}

void items_spawn(const EntitySpawn *spawns) {
    int i = 0;
    const EntitySpawn *s;

    for (s = spawns; s->type != ENT_NONE && i < MAX_ITEMS; s++) {
        if (s->type >= ENT_BOARD && s->type <= ENT_WINE) {
            game.items[i].type = s->type;
            game.items[i].x = s->tx * TILE_SIZE;
            game.items[i].y = s->ty * TILE_SIZE;
            game.items[i].active = 1;
            game.items[i].collected = 0;
            game.items[i].frame = 0;
            game.items[i].float_off = 0;
            i++;
        }
    }
}

/* Simple sine for bobbing */
static int sine_table_small(int t) {
    static const int s[] = { 0, 1, 2, 2, 2, 1, 0, -1, -2, -2, -2, -1 };
    return s[(t >> 2) % 12];
}

void items_update(void) {
    int i;
    Item *it;

    for (i = 0; i < MAX_ITEMS; i++) {
        it = &game.items[i];
        if (!it->active) continue;

        /* Bobbing animation */
        it->frame++;
        it->float_off = sine_table_small(it->frame);
    }
}

void items_draw(struct RastPort *rp, int cam_x) {
    int i, sx, sy;
    Item *it;

    for (i = 0; i < MAX_ITEMS; i++) {
        it = &game.items[i];
        if (!it->active) continue;

        sx = it->x - cam_x;
        sy = it->y + it->float_off;

        if (sx < 0 || sx + 16 > SCREEN_W) continue;
        if (sy < 0 || sy + 16 > HUD_Y) continue;

        switch (it->type) {
        case ENT_BOARD:
            /* Computer circuit board - green with gold traces */
            SetAPen(rp, COL_DKGREEN);
            RectFill(rp, sx + 2, sy + 3, sx + 13, sy + 12);
            SetAPen(rp, COL_YELLOW);
            Move(rp, sx + 4, sy + 5); Draw(rp, sx + 11, sy + 5);
            Move(rp, sx + 4, sy + 8); Draw(rp, sx + 11, sy + 8);
            Move(rp, sx + 7, sy + 3); Draw(rp, sx + 7, sy + 12);
            /* Chips */
            SetAPen(rp, COL_BLACK);
            RectFill(rp, sx + 4, sy + 6, sx + 6, sy + 7);
            RectFill(rp, sx + 9, sy + 9, sx + 11, sy + 10);
            break;

        case ENT_DISKETTE:
            /* 3.5" floppy disk */
            SetAPen(rp, COL_BLUE);
            RectFill(rp, sx + 2, sy + 2, sx + 13, sy + 13);
            /* Metal slider */
            SetAPen(rp, COL_GREY);
            RectFill(rp, sx + 4, sy + 2, sx + 11, sy + 4);
            /* Label */
            SetAPen(rp, COL_WHITE);
            RectFill(rp, sx + 4, sy + 7, sx + 11, sy + 11);
            /* Hub hole */
            SetAPen(rp, COL_BLACK);
            RectFill(rp, sx + 7, sy + 9, sx + 8, sy + 10);
            break;

        case ENT_BALL:
            /* Colorful ball */
            SetAPen(rp, COL_RED);
            RectFill(rp, sx + 4, sy + 3, sx + 11, sy + 12);
            RectFill(rp, sx + 3, sy + 5, sx + 12, sy + 10);
            /* Highlight */
            SetAPen(rp, COL_WHITE);
            RectFill(rp, sx + 5, sy + 4, sx + 7, sy + 6);
            /* Stripe */
            SetAPen(rp, COL_YELLOW);
            Move(rp, sx + 3, sy + 7); Draw(rp, sx + 12, sy + 7);
            break;

        case ENT_PILLOW_C:
            /* Pillow (collectible) */
            SetAPen(rp, COL_WHITE);
            RectFill(rp, sx + 2, sy + 4, sx + 13, sy + 12);
            /* Puffy corners */
            RectFill(rp, sx + 3, sy + 3, sx + 12, sy + 13);
            /* Stitch lines */
            SetAPen(rp, COL_GREY);
            Move(rp, sx + 2, sy + 8); Draw(rp, sx + 13, sy + 8);
            Move(rp, sx + 7, sy + 4); Draw(rp, sx + 7, sy + 12);
            break;

        case ENT_PLANT:
            /* Green potted plant */
            SetAPen(rp, COL_LTGREEN);
            RectFill(rp, sx + 4, sy + 2, sx + 11, sy + 7);
            RectFill(rp, sx + 2, sy + 4, sx + 13, sy + 6);
            /* Stem */
            SetAPen(rp, COL_GREEN);
            RectFill(rp, sx + 7, sy + 7, sx + 8, sy + 10);
            /* Pot */
            SetAPen(rp, COL_ORANGE);
            RectFill(rp, sx + 4, sy + 10, sx + 11, sy + 14);
            SetAPen(rp, COL_DKORANGE);
            Move(rp, sx + 4, sy + 10); Draw(rp, sx + 11, sy + 10);
            break;

        case ENT_BRIEFCASE:
            /* Briefcase power-up */
            SetAPen(rp, COL_BROWN);
            RectFill(rp, sx + 1, sy + 4, sx + 14, sy + 13);
            /* Handle */
            SetAPen(rp, COL_DKBROWN);
            RectFill(rp, sx + 5, sy + 2, sx + 10, sy + 4);
            /* Clasp */
            SetAPen(rp, COL_YELLOW);
            RectFill(rp, sx + 7, sy + 8, sx + 8, sy + 9);
            /* Shine */
            SetAPen(rp, COL_WHITE);
            RectFill(rp, sx + 2, sy + 5, sx + 4, sy + 6);
            break;

        case ENT_HEART:
            /* Heart shape */
            SetAPen(rp, COL_RED);
            RectFill(rp, sx + 3, sy + 4, sx + 7, sy + 8);
            RectFill(rp, sx + 8, sy + 4, sx + 12, sy + 8);
            RectFill(rp, sx + 2, sy + 5, sx + 13, sy + 7);
            RectFill(rp, sx + 4, sy + 8, sx + 11, sy + 11);
            RectFill(rp, sx + 5, sy + 11, sx + 10, sy + 12);
            RectFill(rp, sx + 6, sy + 12, sx + 9, sy + 13);
            /* Highlight */
            SetAPen(rp, COL_WHITE);
            RectFill(rp, sx + 4, sy + 5, sx + 5, sy + 6);
            break;

        case ENT_PILLOW_P:
            /* Pillow power-up (extra life) - with "ZZ" */
            SetAPen(rp, COL_WHITE);
            RectFill(rp, sx + 2, sy + 4, sx + 13, sy + 12);
            RectFill(rp, sx + 3, sy + 3, sx + 12, sy + 13);
            SetAPen(rp, COL_BLUE);
            /* ZZ pattern */
            Move(rp, sx + 5, sy + 6); Draw(rp, sx + 8, sy + 6);
            Draw(rp, sx + 5, sy + 10); Draw(rp, sx + 8, sy + 10);
            break;

        case ENT_BEER:
            /* Beer glass */
            SetAPen(rp, COL_YELLOW);
            RectFill(rp, sx + 4, sy + 4, sx + 11, sy + 13);
            /* Foam */
            SetAPen(rp, COL_WHITE);
            RectFill(rp, sx + 3, sy + 2, sx + 12, sy + 5);
            /* Glass outline */
            SetAPen(rp, COL_GREY);
            Move(rp, sx + 4, sy + 4); Draw(rp, sx + 4, sy + 13);
            Move(rp, sx + 11, sy + 4); Draw(rp, sx + 11, sy + 13);
            /* Handle */
            SetAPen(rp, COL_GREY);
            RectFill(rp, sx + 12, sy + 6, sx + 14, sy + 10);
            break;

        case ENT_WINE:
            /* Wine glass */
            SetAPen(rp, COL_GREY);
            /* Stem */
            RectFill(rp, sx + 7, sy + 9, sx + 8, sy + 12);
            /* Base */
            RectFill(rp, sx + 5, sy + 12, sx + 10, sy + 13);
            /* Bowl */
            SetAPen(rp, COL_RED);
            RectFill(rp, sx + 4, sy + 3, sx + 11, sy + 9);
            RectFill(rp, sx + 5, sy + 2, sx + 10, sy + 3);
            /* Highlight */
            SetAPen(rp, COL_WHITE);
            RectFill(rp, sx + 5, sy + 4, sx + 6, sy + 5);
            break;
        }
    }
}

int items_check_collect(int px, int py, int pw, int ph) {
    int i;
    Item *it;

    for (i = 0; i < MAX_ITEMS; i++) {
        it = &game.items[i];
        if (!it->active) continue;

        if (px + pw > it->x + 2 && px < it->x + 14 &&
            py + ph > it->y + 2 && py < it->y + 14) {
            it->active = 0;
            return it->type;
        }
    }
    return 0;
}
