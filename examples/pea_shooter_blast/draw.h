/*
 * Pea Shooter Blast - Drawing functions
 */
#ifndef DRAW_H
#define DRAW_H

#include <graphics/rastport.h>
#include "game.h"

/* Clear the visible area */
void draw_clear(struct RastPort *rp, ScrollState *sc);

/* Draw tile column at specified map column into bitmap */
void draw_tile_column(struct RastPort *rp, WORD map_col, WORD bitmap_col);

/* Draw all visible tiles (full redraw) */
void draw_all_tiles(struct RastPort *rp, ScrollState *sc);

/* Draw the tank */
void draw_tank(struct RastPort *rp, Tank *tank, ScrollState *sc);

/* Draw player bullets */
void draw_bullets(struct RastPort *rp, Bullet *bullets, ScrollState *sc);

/* Draw enemies */
void draw_enemies(struct RastPort *rp, Enemy *enemies, ScrollState *sc);

/* Draw enemy bullets */
void draw_enemy_bullets(struct RastPort *rp, EnemyBullet *ebullets, ScrollState *sc);

/* Draw particles */
void draw_particles(struct RastPort *rp, Particle *particles, ScrollState *sc);

/* Draw explosions */
void draw_explosions(struct RastPort *rp, Explosion *explosions, ScrollState *sc);

/* Draw powerups */
void draw_powerups(struct RastPort *rp, Powerup *powerups, ScrollState *sc);

/* Draw HUD (health, score, lives, weapon level) */
void draw_hud(struct RastPort *rp, GameState *gs);

/* Draw title screen */
void draw_title(struct RastPort *rp, WORD frame);

/* Draw game over screen */
void draw_gameover(struct RastPort *rp, LONG score);

/* Draw level clear message */
void draw_levelclear(struct RastPort *rp, WORD level);

#endif
