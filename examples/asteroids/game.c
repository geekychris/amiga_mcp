/*
 * Asteroids - Game logic
 */
#include "game.h"

/* Sine/cosine tables: 256 entries, 16.16 fixed-point */
Fixed sin_tab[ANGLE_COUNT];
Fixed cos_tab[ANGLE_COUNT];

/* Default tunables */
Tunables g_tune = {
    80,     /* rock_speed: 0.80 */
    20,     /* ship_thrust: 0.20 */
    400,    /* bullet_speed: 4.00 */
    3,      /* start_lives */
    4,      /* start_rocks */
    2       /* rocks_per_level */
};

/* Precomputed sine table: sin(i * 2*PI / 256) * 65536, 16.16 fixed-point */
static const Fixed sin_precomp[256] = {
        0,   1608,   3216,   4821,   6424,   8022,   9616,  11204,
    12785,  14359,  15924,  17479,  19024,  20557,  22078,  23586,
    25080,  26558,  28020,  29466,  30893,  32303,  33692,  35062,
    36410,  37736,  39040,  40320,  41576,  42806,  44011,  45190,
    46341,  47464,  48559,  49624,  50660,  51665,  52639,  53581,
    54491,  55368,  56212,  57022,  57798,  58538,  59244,  59914,
    60547,  61145,  61705,  62228,  62714,  63162,  63572,  63944,
    64277,  64571,  64827,  65043,  65220,  65358,  65457,  65516,
    65536,  65516,  65457,  65358,  65220,  65043,  64827,  64571,
    64277,  63944,  63572,  63162,  62714,  62228,  61705,  61145,
    60547,  59914,  59244,  58538,  57798,  57022,  56212,  55368,
    54491,  53581,  52639,  51665,  50660,  49624,  48559,  47464,
    46341,  45190,  44011,  42806,  41576,  40320,  39040,  37736,
    36410,  35062,  33692,  32303,  30893,  29466,  28020,  26558,
    25080,  23586,  22078,  20557,  19024,  17479,  15924,  14359,
    12785,  11204,   9616,   8022,   6424,   4821,   3216,   1608,
        0,  -1608,  -3216,  -4821,  -6424,  -8022,  -9616, -11204,
   -12785, -14359, -15924, -17479, -19024, -20557, -22078, -23586,
   -25080, -26558, -28020, -29466, -30893, -32303, -33692, -35062,
   -36410, -37736, -39040, -40320, -41576, -42806, -44011, -45190,
   -46341, -47464, -48559, -49624, -50660, -51665, -52639, -53581,
   -54491, -55368, -56212, -57022, -57798, -58538, -59244, -59914,
   -60547, -61145, -61705, -62228, -62714, -63162, -63572, -63944,
   -64277, -64571, -64827, -65043, -65220, -65358, -65457, -65516,
   -65536, -65516, -65457, -65358, -65220, -65043, -64827, -64571,
   -64277, -63944, -63572, -63162, -62714, -62228, -61705, -61145,
   -60547, -59914, -59244, -58538, -57798, -57022, -56212, -55368,
   -54491, -53581, -52639, -51665, -50660, -49624, -48559, -47464,
   -46341, -45190, -44011, -42806, -41576, -40320, -39040, -37736,
   -36410, -35062, -33692, -32303, -30893, -29466, -28020, -26558,
   -25080, -23586, -22078, -20557, -19024, -17479, -15924, -14359,
   -12785, -11204,  -9616,  -8022,  -6424,  -4821,  -3216,  -1608
};

static void build_trig_tables(void)
{
    int i;
    for (i = 0; i < 256; i++) {
        sin_tab[i] = sin_precomp[i];
        cos_tab[i] = sin_precomp[(i + 64) & 255];
    }
}

void game_init_tables(void)
{
    build_trig_tables();
}

WORD rock_radius(WORD size)
{
    switch (size) {
        case ROCK_LARGE:  return 20;
        case ROCK_MEDIUM: return 12;
        case ROCK_SMALL:  return 6;
    }
    return 6;
}

/* Simple pseudo-random number generator */
static ULONG rng_state = 12345;

static ULONG rng(void)
{
    rng_state = rng_state * 1103515245UL + 12345UL;
    return (rng_state >> 16) & 0x7FFF;
}

static Fixed rng_range(Fixed lo, Fixed hi)
{
    ULONG r = rng();
    return lo + (Fixed)((r * (ULONG)(hi - lo)) >> 15);
}

/* Wrap coordinates to screen */
static void wrap(Fixed *x, Fixed *y)
{
    Fixed w = FIX(SCREEN_W);
    Fixed h = FIX(SCREEN_H);
    if (*x < 0) *x += w;
    if (*x >= w) *x -= w;
    if (*y < 0) *y += h;
    if (*y >= h) *y -= h;
}

void game_init(GameState *gs)
{
    WORD i;
    gs->score = 0;
    gs->lives = (WORD)g_tune.start_lives;
    gs->level = 1;
    gs->state = STATE_PLAYING;
    gs->state_timer = 0;
    gs->rock_count = 0;
    gs->frame = 0;

    /* Init ship at center */
    gs->ship.x = FIX(SCREEN_W / 2);
    gs->ship.y = FIX(SCREEN_H / 2);
    gs->ship.dx = 0;
    gs->ship.dy = 0;
    gs->ship.angle = 192; /* pointing up (270 degrees = 192 in 0..255) */
    gs->ship.alive = 1;
    gs->ship.invuln_timer = SHIP_INVULN_TIME;
    gs->ship.thrust_on = 0;

    /* Clear bullets */
    for (i = 0; i < MAX_BULLETS; i++)
        gs->bullets[i].active = 0;

    /* Clear rocks */
    for (i = 0; i < MAX_ROCKS; i++)
        gs->rocks[i].active = 0;

    /* Clear particles */
    for (i = 0; i < MAX_PARTICLES; i++)
        gs->particles[i].active = 0;

    /* Spawn initial rocks */
    game_spawn_rocks(gs, (WORD)g_tune.start_rocks);
}

void game_spawn_rocks(GameState *gs, WORD count)
{
    WORD i, j;
    Fixed base_speed = (g_tune.rock_speed * 65536L) / 100L;

    for (j = 0; j < count && gs->rock_count < MAX_ROCKS; j++) {
        for (i = 0; i < MAX_ROCKS; i++) {
            if (!gs->rocks[i].active) {
                Rock *r = &gs->rocks[i];
                WORD edge = (WORD)(rng() & 3);

                /* Spawn at screen edge */
                switch (edge) {
                    case 0: r->x = 0;              r->y = rng_range(0, FIX(SCREEN_H)); break;
                    case 1: r->x = FIX(SCREEN_W);  r->y = rng_range(0, FIX(SCREEN_H)); break;
                    case 2: r->x = rng_range(0, FIX(SCREEN_W)); r->y = 0; break;
                    default: r->x = rng_range(0, FIX(SCREEN_W)); r->y = FIX(SCREEN_H); break;
                }

                /* Random velocity */
                {
                    WORD a = (WORD)(rng() & 255);
                    Fixed spd = rng_range(base_speed / 2, base_speed);
                    r->dx = FIX_MUL(cos_tab[a], spd);
                    r->dy = FIX_MUL(sin_tab[a], spd);
                }

                r->size = ROCK_LARGE;
                r->shape = (WORD)(rng() & 3);
                r->angle = (WORD)(rng() & 255);
                r->rot_speed = (WORD)((rng() % 5) - 2);
                r->active = 1;
                gs->rock_count++;
                break;
            }
        }
    }
}

/* Spawn sub-rocks when a rock is destroyed */
static void split_rock(GameState *gs, Rock *parent)
{
    WORD new_size, count, i, j;
    Fixed base_speed;

    if (parent->size == ROCK_SMALL) return; /* smallest, just destroy */

    new_size = parent->size + 1;
    count = 2;
    base_speed = (g_tune.rock_speed * 65536L) / 100L;
    /* Smaller rocks move faster */
    if (new_size == ROCK_MEDIUM) base_speed = (base_speed * 3) / 2;
    if (new_size == ROCK_SMALL) base_speed = base_speed * 2;

    for (j = 0; j < count && gs->rock_count < MAX_ROCKS; j++) {
        for (i = 0; i < MAX_ROCKS; i++) {
            if (!gs->rocks[i].active) {
                Rock *r = &gs->rocks[i];
                WORD a = (WORD)(rng() & 255);
                Fixed spd = rng_range(base_speed / 2, base_speed);

                r->x = parent->x;
                r->y = parent->y;
                r->dx = FIX_MUL(cos_tab[a], spd);
                r->dy = FIX_MUL(sin_tab[a], spd);
                r->size = new_size;
                r->shape = (WORD)(rng() & 3);
                r->angle = (WORD)(rng() & 255);
                r->rot_speed = (WORD)((rng() % 7) - 3);
                r->active = 1;
                gs->rock_count++;
                break;
            }
        }
    }
}

void game_spawn_particles(GameState *gs, Fixed x, Fixed y,
                          WORD count, WORD color)
{
    WORD i, j;
    for (j = 0; j < count; j++) {
        for (i = 0; i < MAX_PARTICLES; i++) {
            if (!gs->particles[i].active) {
                Particle *p = &gs->particles[i];
                WORD a = (WORD)(rng() & 255);
                Fixed spd = rng_range(FIX(0), FIX(3));
                p->x = x;
                p->y = y;
                p->dx = FIX_MUL(cos_tab[a], spd);
                p->dy = FIX_MUL(sin_tab[a], spd);
                p->life = (WORD)(10 + (rng() % 20));
                p->color = color;
                p->active = 1;
                break;
            }
        }
    }
}

/* Circle-vs-circle collision */
static WORD collide(Fixed x1, Fixed y1, WORD r1, Fixed x2, Fixed y2, WORD r2)
{
    Fixed dx = x1 - x2;
    Fixed dy = y1 - y2;
    LONG dist2, rad;

    /* Use integer pixels for collision to avoid overflow */
    WORD px = (WORD)(dx >> 16);
    WORD py = (WORD)(dy >> 16);
    dist2 = (LONG)px * px + (LONG)py * py;
    rad = (LONG)(r1 + r2);
    return dist2 < rad * rad;
}

static void fire_bullet(GameState *gs)
{
    WORD i;
    Fixed bspd = (g_tune.bullet_speed * 65536L) / 100L;

    for (i = 0; i < MAX_BULLETS; i++) {
        if (!gs->bullets[i].active) {
            Bullet *b = &gs->bullets[i];
            b->x = gs->ship.x;
            b->y = gs->ship.y;
            b->dx = FIX_MUL(cos_tab[gs->ship.angle & 255], bspd);
            b->dy = FIX_MUL(sin_tab[gs->ship.angle & 255], bspd);
            b->life = BULLET_LIFE;
            b->active = 1;
            break;
        }
    }
}

/* Sound effect callbacks (defined in main.c) */
extern void sfx_shoot(void);
extern void sfx_explode_large(void);
extern void sfx_explode_small(void);
extern void sfx_thrust_tick(void);
extern void sfx_die(void);

void game_update(GameState *gs, WORD joy_left, WORD joy_right,
                 WORD joy_up, WORD joy_fire)
{
    WORD i, j;
    Fixed thrust_accel;

    gs->frame++;

    if (gs->state == STATE_GAMEOVER) {
        gs->state_timer--;
        if (gs->state_timer <= 0) {
            game_init(gs);
        }
        /* Still update particles */
        goto update_particles;
    }

    if (gs->state == STATE_DEAD) {
        gs->state_timer--;
        if (gs->state_timer <= 0) {
            /* Respawn */
            gs->ship.x = FIX(SCREEN_W / 2);
            gs->ship.y = FIX(SCREEN_H / 2);
            gs->ship.dx = 0;
            gs->ship.dy = 0;
            gs->ship.alive = 1;
            gs->ship.invuln_timer = SHIP_INVULN_TIME;
            gs->state = STATE_PLAYING;
        }
        goto update_rocks_and_particles;
    }

    /* -- Ship controls -- */
    if (gs->ship.alive) {
        if (joy_left) {
            gs->ship.angle = (gs->ship.angle - SHIP_TURN_SPEED) & 255;
        }
        if (joy_right) {
            gs->ship.angle = (gs->ship.angle + SHIP_TURN_SPEED) & 255;
        }

        thrust_accel = (g_tune.ship_thrust * 65536L) / 100L;
        gs->ship.thrust_on = joy_up;
        if (joy_up) {
            gs->ship.dx += FIX_MUL(cos_tab[gs->ship.angle & 255], thrust_accel);
            gs->ship.dy += FIX_MUL(sin_tab[gs->ship.angle & 255], thrust_accel);

            /* Clamp speed */
            {
                WORD vx = (WORD)(gs->ship.dx >> 8);
                WORD vy = (WORD)(gs->ship.dy >> 8);
                LONG spd2 = (LONG)vx * vx + (LONG)vy * vy;
                LONG max2 = (LONG)(SHIP_MAX_SPEED >> 8) * (SHIP_MAX_SPEED >> 8);
                if (spd2 > max2) {
                    gs->ship.dx = (gs->ship.dx * 15) / 16;
                    gs->ship.dy = (gs->ship.dy * 15) / 16;
                }
            }
        }

        /* Drag */
        gs->ship.dx = (gs->ship.dx * 253L) / 256L;
        gs->ship.dy = (gs->ship.dy * 253L) / 256L;

        /* Move ship */
        gs->ship.x += gs->ship.dx;
        gs->ship.y += gs->ship.dy;
        wrap(&gs->ship.x, &gs->ship.y);

        /* Invulnerability countdown */
        if (gs->ship.invuln_timer > 0)
            gs->ship.invuln_timer--;

        /* Fire */
        if (joy_fire) {
            /* Rate limit: every 8 frames */
            if ((gs->frame & 7) == 0) {
                fire_bullet(gs);
                sfx_shoot();
            }
        }
    }

    /* -- Update bullets -- */
    for (i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &gs->bullets[i];
        if (!b->active) continue;
        b->x += b->dx;
        b->y += b->dy;
        wrap(&b->x, &b->y);
        b->life--;
        if (b->life <= 0) b->active = 0;
    }

    /* -- Bullet vs Rock collision -- */
    for (i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &gs->bullets[i];
        if (!b->active) continue;
        for (j = 0; j < MAX_ROCKS; j++) {
            Rock *r = &gs->rocks[j];
            if (!r->active) continue;
            if (collide(b->x, b->y, 2, r->x, r->y, rock_radius(r->size))) {
                b->active = 0;

                /* Score */
                switch (r->size) {
                    case ROCK_LARGE:  gs->score += SCORE_LARGE; break;
                    case ROCK_MEDIUM: gs->score += SCORE_MEDIUM; break;
                    case ROCK_SMALL:  gs->score += SCORE_SMALL; break;
                }

                /* Particles */
                game_spawn_particles(gs, r->x, r->y,
                    r->size == ROCK_LARGE ? 12 : (r->size == ROCK_MEDIUM ? 8 : 5),
                    r->size == ROCK_SMALL ? 7 : 15);

                /* Split */
                split_rock(gs, r);

                /* Destroy rock */
                r->active = 0;
                gs->rock_count--;

                if (r->size == ROCK_LARGE)
                    sfx_explode_large();
                else
                    sfx_explode_small();

                break;
            }
        }
    }

    /* -- Ship vs Rock collision -- */
    if (gs->ship.alive && gs->ship.invuln_timer == 0) {
        for (j = 0; j < MAX_ROCKS; j++) {
            Rock *r = &gs->rocks[j];
            if (!r->active) continue;
            if (collide(gs->ship.x, gs->ship.y, SHIP_RADIUS,
                        r->x, r->y, rock_radius(r->size))) {
                /* Ship destroyed */
                gs->ship.alive = 0;
                game_spawn_particles(gs, gs->ship.x, gs->ship.y, 20, 9);
                sfx_die();
                gs->lives--;
                if (gs->lives <= 0) {
                    gs->state = STATE_GAMEOVER;
                    gs->state_timer = 150; /* 3 seconds */
                } else {
                    gs->state = STATE_DEAD;
                    gs->state_timer = 100; /* 2 seconds */
                }
                break;
            }
        }
    }

update_rocks_and_particles:
    /* -- Update rocks -- */
    for (i = 0; i < MAX_ROCKS; i++) {
        Rock *r = &gs->rocks[i];
        if (!r->active) continue;
        r->x += r->dx;
        r->y += r->dy;
        wrap(&r->x, &r->y);
    }

    /* Check if all rocks destroyed -> next level */
    if (gs->rock_count <= 0 && gs->state == STATE_PLAYING) {
        gs->level++;
        game_spawn_rocks(gs, (WORD)(g_tune.start_rocks +
                         (gs->level - 1) * g_tune.rocks_per_level));
    }

update_particles:
    /* -- Update particles -- */
    for (i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &gs->particles[i];
        if (!p->active) continue;
        p->x += p->dx;
        p->y += p->dy;
        /* Slow down */
        p->dx = (p->dx * 240L) / 256L;
        p->dy = (p->dy * 240L) / 256L;
        p->life--;
        if (p->life <= 0) p->active = 0;
    }
}
