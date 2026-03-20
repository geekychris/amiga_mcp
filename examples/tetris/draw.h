#ifndef DRAW_H
#define DRAW_H

#include <graphics/rastport.h>

/* Forward declare */
struct GameState;

/* Palette */
#define NUM_COLORS 16
extern const UWORD palette[NUM_COLORS];

/* Field drawing position */
#define FIELD_X 20
#define FIELD_Y 8
#define CELL_SIZE 12

void draw_clear(struct RastPort *rp);
void draw_field(struct RastPort *rp, const struct GameState *gs);
void draw_current_piece(struct RastPort *rp, const struct GameState *gs);
void draw_ghost_piece(struct RastPort *rp, const struct GameState *gs);
void draw_next_piece(struct RastPort *rp, const struct GameState *gs);
void draw_hud(struct RastPort *rp, const struct GameState *gs);
void draw_title(struct RastPort *rp);
void draw_gameover(struct RastPort *rp, const struct GameState *gs);
void draw_line_clear_flash(struct RastPort *rp, const struct GameState *gs);
void draw_paused(struct RastPort *rp);

#endif
