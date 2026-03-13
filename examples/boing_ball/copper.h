#ifndef COPPER_H
#define COPPER_H

#include <exec/types.h>
#include <graphics/view.h>
#include <graphics/copper.h>

/* Sky gradient from dark blue (top) to light blue (horizon) */
#define SKY_TOP_Y     0
#define SKY_BOTTOM_Y  180   /* Where sky meets floor */
#define COPPER_LINES  (SKY_BOTTOM_Y - SKY_TOP_Y)

void copper_init(struct ViewPort *vp);
void copper_set_rainbow(struct ViewPort *vp, BOOL enable);
void copper_cleanup(void);

#endif
