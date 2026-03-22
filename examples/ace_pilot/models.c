/*
 * models.c - Wireframe model data for Ace Pilot
 * All coordinates in model-local space, Y-up
 */
#include "models.h"

/* --- Biplane --- */
/* Side view: nose->tail with upper/lower wings, tail fin */
static const WORD biplane_verts[][3] = {
    {  0,  0,  8},   /* 0: nose */
    {  0,  0, -8},   /* 1: tail */
    {-12,  3,  0},   /* 2: upper wing left */
    { 12,  3,  0},   /* 3: upper wing right */
    {-12, -2,  0},   /* 4: lower wing left */
    { 12, -2,  0},   /* 5: lower wing right */
    {  0,  5, -8},   /* 6: tail fin top */
    { -4,  0, -8},   /* 7: tail plane left */
    {  4,  0, -8},   /* 8: tail plane right */
};
static const WORD biplane_edges[][2] = {
    {0, 2}, {0, 3},   /* nose to upper wings */
    {0, 4}, {0, 5},   /* nose to lower wings */
    {2, 3},            /* upper wing span */
    {4, 5},            /* lower wing span */
    {2, 4},            /* left strut */
    {3, 5},            /* right strut */
    {1, 6},            /* tail fin */
    {7, 8},            /* tail plane */
    {1, 7}, {1, 8},   /* tail to tail plane */
};

/* --- Blimp --- */
/* Elongated oval shape with gondola */
static const WORD blimp_verts[][3] = {
    {  0,  0, 20},   /* 0: nose */
    {  0,  0,-20},   /* 1: tail */
    {  0,  6,  0},   /* 2: top */
    {  0, -4,  0},   /* 3: bottom */
    { -6,  0,  0},   /* 4: left */
    {  6,  0,  0},   /* 5: right */
    {  0, -8, -2},   /* 6: gondola */
    {  0,  3,-20},   /* 7: tail fin top */
    { -4,  0,-20},   /* 8: tail fin left */
    {  4,  0,-20},   /* 9: tail fin right */
};
static const WORD blimp_edges[][2] = {
    {0, 2}, {0, 3}, {0, 4}, {0, 5},  /* nose to sides */
    {1, 2}, {1, 3}, {1, 4}, {1, 5},  /* tail to sides */
    {2, 4}, {4, 3}, {3, 5}, {5, 2},  /* cross-section ring */
    {3, 6},                            /* gondola strut */
    {1, 7}, {1, 8}, {1, 9},          /* tail fins */
};

/* --- Hangar --- */
/* Rectangular box */
static const WORD hangar_verts[][3] = {
    {-15,  0,-10},   /* 0: front-left-bottom */
    { 15,  0,-10},   /* 1: front-right-bottom */
    { 15, 12,-10},   /* 2: front-right-top */
    {-15, 12,-10},   /* 3: front-left-top */
    {-15,  0, 10},   /* 4: back-left-bottom */
    { 15,  0, 10},   /* 5: back-right-bottom */
    { 15, 12, 10},   /* 6: back-right-top */
    {-15, 12, 10},   /* 7: back-left-top */
};
static const WORD hangar_edges[][2] = {
    {0,1}, {1,2}, {2,3}, {3,0},  /* front face */
    {4,5}, {5,6}, {6,7}, {7,4},  /* back face */
    {0,4}, {1,5}, {2,6}, {3,7},  /* connecting edges */
};

/* --- Fuel Tank --- */
/* Short squat cylinder approximated as hexagonal prism */
static const WORD fuel_verts[][3] = {
    { -4,  0, -6},   /* 0 */
    {  4,  0, -6},   /* 1 */
    {  7,  0,  0},   /* 2 */
    {  4,  0,  6},   /* 3 */
    { -4,  0,  6},   /* 4 */
    { -7,  0,  0},   /* 5 */
    { -4,  8, -6},   /* 6 */
    {  4,  8, -6},   /* 7 */
    {  7,  8,  0},   /* 8 */
    {  4,  8,  6},   /* 9 */
    { -4,  8,  6},   /* 10 */
    { -7,  8,  0},   /* 11 */
};
static const WORD fuel_edges[][2] = {
    {0,1}, {1,2}, {2,3}, {3,4}, {4,5}, {5,0},     /* bottom hex */
    {6,7}, {7,8}, {8,9}, {9,10}, {10,11}, {11,6}, /* top hex */
    {0,6}, {1,7}, {2,8}, {3,9}, {4,10}, {5,11},   /* verticals */
};

/* --- Runway --- */
/* Flat rectangle on ground with center stripe */
static const WORD runway_verts[][3] = {
    { -8, 0, -40},  /* 0: left-near */
    {  8, 0, -40},  /* 1: right-near */
    {  8, 0,  40},  /* 2: right-far */
    { -8, 0,  40},  /* 3: left-far */
    {  0, 0, -40},  /* 4: center-near */
    {  0, 0,  40},  /* 5: center-far */
};
static const WORD runway_edges[][2] = {
    {0,1}, {1,2}, {2,3}, {3,0},  /* outline */
    {4,5},                        /* center stripe */
};

/* --- Pyramid --- */
/* 4-sided pyramid */
static const WORD pyramid_verts[][3] = {
    {-10, 0,-10},   /* 0: base corners */
    { 10, 0,-10},   /* 1 */
    { 10, 0, 10},   /* 2 */
    {-10, 0, 10},   /* 3 */
    {  0, 18,  0},  /* 4: apex */
};
static const WORD pyramid_edges[][2] = {
    {0,1}, {1,2}, {2,3}, {3,0},  /* base */
    {0,4}, {1,4}, {2,4}, {3,4},  /* sides to apex */
};

/* --- Tree --- */
/* Simple triangle-on-stick (side view) */
static const WORD tree_verts[][3] = {
    {  0,  0,  0},   /* 0: trunk base */
    {  0,  6,  0},   /* 1: trunk top / crown base */
    { -4,  6,  0},   /* 2: crown left */
    {  4,  6,  0},   /* 3: crown right */
    {  0,  6, -4},   /* 4: crown back */
    {  0,  6,  4},   /* 5: crown front */
    {  0, 14,  0},   /* 6: crown apex */
};
static const WORD tree_edges[][2] = {
    {0, 1},           /* trunk */
    {2, 6}, {3, 6},   /* side edges to apex */
    {4, 6}, {5, 6},   /* front/back to apex */
    {2, 4}, {4, 3}, {3, 5}, {5, 2},  /* crown base ring */
};

/* --- Bullet --- */
/* Simple 2-vert line */
static const WORD bullet_verts[][3] = {
    { 0, 0, 0},
    { 0, 0, 3},
};
static const WORD bullet_edges[][2] = {
    {0, 1},
};

/* --- Model table --- */
const Model models[MODEL_COUNT] = {
    /* MODEL_BIPLANE */
    { 9, 12, biplane_verts, biplane_edges, 15 },
    /* MODEL_BLIMP */
    { 10, 15, blimp_verts, blimp_edges, 22 },
    /* MODEL_HANGAR */
    { 8, 12, hangar_verts, hangar_edges, 18 },
    /* MODEL_FUEL_TANK */
    { 12, 18, fuel_verts, fuel_edges, 10 },
    /* MODEL_RUNWAY */
    { 6, 5, runway_verts, runway_edges, 42 },
    /* MODEL_PYRAMID */
    { 5, 8, pyramid_verts, pyramid_edges, 18 },
    /* MODEL_TREE */
    { 7, 9, tree_verts, tree_edges, 14 },
    /* MODEL_BULLET */
    { 2, 1, bullet_verts, bullet_edges, 2 },
};
