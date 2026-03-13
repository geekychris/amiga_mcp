/*
 * render.h - 3D wireframe rendering for Red Baron
 */
#ifndef RENDER_H
#define RENDER_H

#include <exec/types.h>
#include <graphics/rastport.h>
#include "game.h"

/* Render the 3D scene for one player */
void render_scene(struct RastPort *rp, GameWorld *w, WORD player_idx,
                  const Viewport *vp);

/* Render HUD overlay for one player */
void render_hud(struct RastPort *rp, GameWorld *w, WORD player_idx,
                const Viewport *vp);

/* Render title screen */
void render_title(struct RastPort *rp);

/* Render game over screen */
void render_gameover(struct RastPort *rp, GameWorld *w);

/* Render split-screen divider line */
void render_divider(struct RastPort *rp);

#endif
