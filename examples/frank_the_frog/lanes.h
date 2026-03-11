/*
 * Frank the Frog - Lane objects (traffic + river)
 */
#ifndef LANES_H
#define LANES_H

#include <exec/types.h>
#include <graphics/rastport.h>

/* Initialize all lanes for a given level */
void lanes_init(int level);

/* Advance all lane objects by one frame */
void lanes_tick(void);

/* Draw all lane objects */
void lanes_draw(struct RastPort *rp);

/* Check if pixel position (px,py) collides with a vehicle. Returns 1 if hit. */
int lanes_check_car(int px, int py);

/*
 * Check if pixel position (px,py) is on a log/turtle.
 * Returns 0 if in water (death), or the lane's pixel speed (with sign)
 * so the caller can ride the frog along.
 */
int lanes_check_river(int px, int py, int *ride_dx);

/* Check if frog is on a valid home base slot. Returns slot index (0-4) or -1. */
int lanes_check_home(int px);

/* Mark a home slot as filled */
void lanes_fill_home(int slot);

/* Returns 1 if all 5 home slots are filled */
int lanes_all_home(void);

/* Reset home slots for new level */
void lanes_reset_home(void);

#endif /* LANES_H */
