/*
 * Frank the Frog - Player control
 */
#ifndef FRANK_H
#define FRANK_H

#include <exec/types.h>
#include <graphics/rastport.h>

/* Initialize Frank at start position */
void frank_init(void);

/* Move Frank by one tile (dx/dy = -1, 0, or 1) */
void frank_move(int dx, int dy);

/* Draw Frank at current position */
void frank_draw(struct RastPort *rp);

/* Erase Frank (draw background color at his position) */
void frank_erase(struct RastPort *rp);

/* Get Frank's grid position */
int frank_col(void);
int frank_row(void);

/* Get Frank's pixel position */
int frank_x(void);
int frank_y(void);

/* Set Frank's pixel X directly (for riding logs) */
void frank_set_x(int x);

/* Start death animation, returns 1 while animating, 0 when done */
int frank_die_tick(void);
void frank_start_death(void);

/* Check if Frank is in death animation */
int frank_is_dying(void);

/* Get highest row reached this life */
int frank_highest_row(void);

#endif /* FRANK_H */
