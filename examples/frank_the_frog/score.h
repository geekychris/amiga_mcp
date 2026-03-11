/*
 * Frank the Frog - Score and HUD
 */
#ifndef SCORE_H
#define SCORE_H

#include <graphics/rastport.h>

void score_init(void);
void score_add(int points);
void score_draw(struct RastPort *rp);
int  score_get(void);
int  score_lives(void);
void score_lose_life(void);
int  score_level(void);
void score_next_level(void);

#endif /* SCORE_H */
