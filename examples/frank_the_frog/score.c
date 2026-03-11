/*
 * Frank the Frog - Score and HUD
 */

#include <exec/types.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <proto/graphics.h>

#include <stdio.h>
#include <string.h>

#include "game.h"
#include "score.h"

static int current_score = 0;
static int high_score = 0;
static int lives = 3;
static int level = 1;

void score_init(void)
{
    current_score = 0;
    lives = 3;
    level = 1;
}

void score_add(int points)
{
    current_score += points;
    if (current_score > high_score) {
        high_score = current_score;
    }
}

int score_get(void)    { return current_score; }
int score_lives(void)  { return lives; }
int score_level(void)  { return level; }

void score_lose_life(void)
{
    if (lives > 0) lives--;
}

void score_next_level(void)
{
    level++;
    score_add(1000);
}

void score_draw(struct RastPort *rp)
{
    char buf[64];
    int i;

    /* Clear HUD area */
    SetAPen(rp, (long)COL_HUD_BG);
    RectFill(rp, 0, HUD_Y, SCREEN_W - 1, SCREEN_H - 1);

    /* Score */
    SetAPen(rp, (long)COL_WHITE);
    SetBPen(rp, (long)COL_HUD_BG);

    sprintf(buf, "SCORE %06ld", (long)current_score);
    Move(rp, 8, HUD_Y + 12);
    Text(rp, (CONST_STRPTR)buf, (long)strlen(buf));

    sprintf(buf, "HI %06ld", (long)high_score);
    Move(rp, 130, HUD_Y + 12);
    Text(rp, (CONST_STRPTR)buf, (long)strlen(buf));

    sprintf(buf, "LVL %ld", (long)level);
    Move(rp, 260, HUD_Y + 12);
    Text(rp, (CONST_STRPTR)buf, (long)strlen(buf));

    /* Lives as small frog icons */
    SetAPen(rp, (long)COL_FROG);
    for (i = 0; i < lives && i < 5; i++) {
        RectFill(rp, 8 + i * 14, HUD_Y + 22, 8 + i * 14 + 8, HUD_Y + 30);
    }
}
