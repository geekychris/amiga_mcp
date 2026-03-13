/*
 * RJ and Dale's Super Bros - Title Screen
 * Character select and game start
 */
#include "game.h"

static void draw_big_text(struct RastPort *rp, int x, int y, const char *str, int color) {
    int i, cx;
    /* Draw text with a shadow effect */
    SetAPen(rp, COL_BLACK);
    Move(rp, x + 1, y + 1);
    for (i = 0; str[i]; i++) {}
    Text(rp, str, (long)i);

    SetAPen(rp, color);
    Move(rp, x, y);
    Text(rp, str, (long)i);

    /* Unused but silences compiler */
    cx = x;
    (void)cx;
}

static void draw_character_preview(struct RastPort *rp, int x, int y, int character, int selected) {
    int bw, ph;

    /* Selection box */
    if (selected) {
        SetAPen(rp, COL_YELLOW);
        RectFill(rp, x - 2, y - 2, x + 30, y + 42);
    }

    /* Background */
    SetAPen(rp, COL_SKY2);
    RectFill(rp, x, y, x + 28, y + 40);

    if (character == CHAR_RJ) {
        bw = 10; ph = 28;
        /* RJ */
        SetAPen(rp, COL_GREY);
        RectFill(rp, x + 10, y + 4, x + 19, y + 6);
        SetAPen(rp, COL_SKIN);
        RectFill(rp, x + 10, y + 7, x + 19, y + 12);
        SetAPen(rp, COL_BLACK);
        RectFill(rp, x + 11, y + 12, x + 18, y + 13);
        RectFill(rp, x + 15, y + 8, x + 16, y + 9);
        SetAPen(rp, COL_WHITE);
        RectFill(rp, x + 9, y + 14, x + 20, y + 22);
        SetAPen(rp, COL_BLUE);
        RectFill(rp, x + 9, y + 23, x + 20, y + 30);
        SetAPen(rp, COL_BROWN);
        RectFill(rp, x + 9, y + 31, x + 20, y + 34);
    } else {
        bw = 14; ph = 24;
        /* Dale */
        SetAPen(rp, COL_GREY);
        RectFill(rp, x + 8, y + 6, x + 21, y + 8);
        SetAPen(rp, COL_SKIN);
        RectFill(rp, x + 8, y + 9, x + 21, y + 14);
        SetAPen(rp, COL_BLACK);
        RectFill(rp, x + 9, y + 14, x + 20, y + 15);
        RectFill(rp, x + 15, y + 10, x + 16, y + 11);
        SetAPen(rp, COL_WHITE);
        RectFill(rp, x + 7, y + 16, x + 22, y + 22);
        SetAPen(rp, COL_BLUE);
        RectFill(rp, x + 8, y + 23, x + 21, y + 28);
        SetAPen(rp, COL_BROWN);
        RectFill(rp, x + 8, y + 29, x + 21, y + 32);
    }

    (void)bw;
    (void)ph;
}

/*
 * Title screen - returns selected character (CHAR_RJ or CHAR_DALE)
 * Also sets game.num_players
 */
int title_screen(struct RastPort *rp) {
    int selected = CHAR_RJ;
    int num_players = 1;
    int blink = 0;
    UWORD inp;

    /* Draw background */
    SetAPen(rp, COL_SKY);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);

    /* Ground */
    SetAPen(rp, COL_GREEN);
    RectFill(rp, 0, 200, SCREEN_W - 1, 205);
    SetAPen(rp, COL_BROWN);
    RectFill(rp, 0, 206, SCREEN_W - 1, SCREEN_H - 1);

    /* Title */
    draw_big_text(rp, 40, 30, "RJ AND DALE'S", COL_WHITE);
    draw_big_text(rp, 60, 50, "SUPER BROS", COL_YELLOW);

    /* Decorative bugs */
    SetAPen(rp, COL_BROWN);
    RectFill(rp, 20, 180, 32, 192);
    SetAPen(rp, COL_DKBROWN);
    RectFill(rp, 22, 176, 30, 180);

    SetAPen(rp, COL_RED);
    RectFill(rp, 280, 185, 290, 195);
    RectFill(rp, 282, 181, 288, 185);

    /* Show and swap so title is visible */
    gfx_swap();

    /* Interactive selection loop */
    for (;;) {
        struct RastPort *brp = gfx_backbuffer();

        /* Redraw background */
        SetAPen(brp, COL_SKY);
        RectFill(brp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);
        SetAPen(brp, COL_GREEN);
        RectFill(brp, 0, 200, SCREEN_W - 1, 205);
        SetAPen(brp, COL_BROWN);
        RectFill(brp, 0, 206, SCREEN_W - 1, SCREEN_H - 1);

        /* Title */
        draw_big_text(brp, 40, 30, "RJ AND DALE'S", COL_WHITE);
        draw_big_text(brp, 60, 50, "SUPER BROS", COL_YELLOW);

        /* Players mode */
        if (num_players == 1) {
            draw_big_text(brp, 90, 80, "1 PLAYER", COL_WHITE);
        } else {
            draw_big_text(brp, 90, 80, "2 PLAYERS", COL_WHITE);
        }
        draw_big_text(brp, 50, 92, "UP/DOWN:PLAYERS", COL_GREY);

        /* Character previews */
        draw_character_preview(brp, 100, 110, CHAR_RJ, selected == CHAR_RJ);
        draw_character_preview(brp, 190, 110, CHAR_DALE, selected == CHAR_DALE);

        /* Labels */
        draw_big_text(brp, 105, 160, "RJ", selected == CHAR_RJ ? COL_YELLOW : COL_WHITE);
        draw_big_text(brp, 190, 160, "DALE", selected == CHAR_DALE ? COL_YELLOW : COL_WHITE);

        draw_big_text(brp, 50, 170, "LEFT/RIGHT:SELECT", COL_GREY);

        /* Blink prompt */
        blink++;
        if ((blink >> 4) & 1) {
            draw_big_text(brp, 60, 190, "PRESS FIRE TO START", COL_RED);
        }

        gfx_swap();

        /* Read input */
        inp = input_read();

        if (inp & INP_LEFT) {
            selected = CHAR_RJ;
            Delay(5);
        }
        if (inp & INP_RIGHT) {
            selected = CHAR_DALE;
            Delay(5);
        }
        if (inp & INP_UP) {
            num_players = (num_players == 1) ? 2 : 1;
            Delay(10);
        }
        if (inp & INP_DOWN) {
            num_players = (num_players == 1) ? 2 : 1;
            Delay(10);
        }
        if ((inp & INP_JUMP) || (inp & INP_START)) {
            game.num_players = num_players;
            return selected;
        }
        if (input_check_esc()) {
            return -1; /* quit */
        }
    }
}
