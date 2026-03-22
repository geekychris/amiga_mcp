/*
 * Planet Patrol - High score declarations
 */
#ifndef SCORE_H
#define SCORE_H

#include "game.h"

void score_init(ScoreTable *st);
void score_load(ScoreTable *st);
void score_save(ScoreTable *st);
WORD score_qualifies(ScoreTable *st, LONG score);
void score_insert(ScoreTable *st, WORD rank, const char *name, LONG score);

#endif
