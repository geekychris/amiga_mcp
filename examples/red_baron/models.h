/*
 * models.h - Wireframe model definitions for Red Baron
 */
#ifndef MODELS_H
#define MODELS_H

#include <exec/types.h>

/* Wireframe model: vertex array + edge index array */
typedef struct {
    WORD num_verts;
    WORD num_edges;
    const WORD (*verts)[3];   /* array of {x, y, z} */
    const WORD (*edges)[2];   /* array of {v1, v2} indices */
    WORD radius;              /* bounding sphere radius for collision */
} Model;

/* Model IDs */
#define MODEL_BIPLANE    0
#define MODEL_BLIMP      1
#define MODEL_HANGAR     2
#define MODEL_FUEL_TANK  3
#define MODEL_RUNWAY     4
#define MODEL_PYRAMID    5
#define MODEL_TREE       6
#define MODEL_BULLET     7
#define MODEL_COUNT      8

extern const Model models[MODEL_COUNT];

#endif
