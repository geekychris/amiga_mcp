/*
 * RJ and Dale's Super Bros - HUD
 * Score, lives, health hearts, level name
 */
#include "game.h"

static void draw_heart(struct RastPort *rp, int x, int y, int filled) {
    int color = filled ? COL_RED : COL_DKBROWN;
    SetAPen(rp, color);
    RectFill(rp, x, y + 1, x + 2, y + 4);
    RectFill(rp, x + 3, y + 1, x + 5, y + 4);
    RectFill(rp, x, y + 2, x + 5, y + 3);
    RectFill(rp, x + 1, y + 4, x + 4, y + 5);
    RectFill(rp, x + 2, y + 5, x + 3, y + 6);
}

static void draw_number(struct RastPort *rp, int x, int y, long val) {
    char buf[12];
    int i, len;
    long v = val;

    if (v < 0) v = 0;

    /* Convert to string */
    if (v == 0) {
        buf[0] = '0';
        buf[1] = 0;
        len = 1;
    } else {
        len = 0;
        while (v > 0 && len < 10) {
            buf[len++] = '0' + (char)(v % 10);
            v /= 10;
        }
        buf[len] = 0;
        /* reverse */
        for (i = 0; i < len / 2; i++) {
            char tmp = buf[i];
            buf[i] = buf[len - 1 - i];
            buf[len - 1 - i] = tmp;
        }
    }

    Move(rp, x, y + 6);
    Text(rp, buf, (long)len);
}

void hud_draw(struct RastPort *rp, Player *p, int level) {
    int i;

    /* Background */
    SetAPen(rp, COL_BLACK);
    RectFill(rp, 0, HUD_Y, SCREEN_W - 1, SCREEN_H - 1);

    /* Divider line */
    SetAPen(rp, COL_GREY);
    Move(rp, 0, HUD_Y);
    Draw(rp, SCREEN_W - 1, HUD_Y);

    /* Character name */
    SetAPen(rp, COL_WHITE);
    Move(rp, 4, HUD_Y + 10);
    if (p->character == CHAR_RJ)
        Text(rp, "RJ", 2L);
    else
        Text(rp, "DALE", 4L);

    /* Health hearts */
    for (i = 0; i < p->max_health; i++) {
        draw_heart(rp, 4 + i * 8, HUD_Y + 16, i < p->health);
    }

    /* Score */
    SetAPen(rp, COL_YELLOW);
    Move(rp, 100, HUD_Y + 10);
    Text(rp, "SCORE", 5L);
    SetAPen(rp, COL_WHITE);
    draw_number(rp, 100, HUD_Y + 16, p->score);

    /* Lives */
    SetAPen(rp, COL_YELLOW);
    Move(rp, 200, HUD_Y + 10);
    Text(rp, "LIVES", 5L);
    SetAPen(rp, COL_WHITE);
    draw_number(rp, 200, HUD_Y + 16, (long)p->lives);

    /* Level */
    SetAPen(rp, COL_YELLOW);
    Move(rp, 260, HUD_Y + 10);
    Text(rp, "LV", 2L);
    SetAPen(rp, COL_WHITE);
    draw_number(rp, 280, HUD_Y + 10, (long)(level + 1));
}
