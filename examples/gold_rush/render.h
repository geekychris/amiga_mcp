/*
 * Gold Rush - Rendering
 */
#ifndef RENDER_H
#define RENDER_H

#include <graphics/rastport.h>
#include "game.h"

/* Render the tile playfield */
void render_playfield(struct RastPort *rp, GameState *gs);

/* Render player and enemies */
void render_entities(struct RastPort *rp, GameState *gs);

/* Render the status bar (score, lives, level, gold) */
void render_status(struct RastPort *rp, GameState *gs);

/* Render the title screen */
void render_title(struct RastPort *rp, GameState *gs);

/* Render game over overlay */
void render_gameover(struct RastPort *rp, GameState *gs);

/* Render level complete overlay */
void render_level_done(struct RastPort *rp, GameState *gs);

#endif /* RENDER_H */
