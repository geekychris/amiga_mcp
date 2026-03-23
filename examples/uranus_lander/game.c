/*
 * Uranus Lander - Game logic
 */
#include <proto/dos.h>
#include <string.h>
#include "game.h"

/* Trig tables */
Fixed sin_tab[ANGLE_COUNT];
Fixed cos_tab[ANGLE_COUNT];

/* Tunables */
Tunables g_tune = {
    GRAVITY_DEFAULT,
    THRUST_DEFAULT,
    FUEL_DEFAULT,
    SAFE_VY_DEFAULT,
    SAFE_VX_DEFAULT,
    SAFE_ANGLE_DEFAULT,
    TURN_RATE_DEFAULT,
    START_LIVES_DEFAULT
};

/* Precomputed sine table: sin(i * 2*PI / 256) * 65536 */
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

/* RNG */
static ULONG rng_state = 12345;
static WORD rng_next(void)
{
    rng_state = rng_state * 1103515245UL + 12345UL;
    return (WORD)((rng_state >> 16) & 0x7FFF);
}

void game_init_tables(void)
{
    int i;
    for (i = 0; i < 256; i++) {
        sin_tab[i] = sin_precomp[i];
        cos_tab[i] = sin_precomp[(i + 64) & 255];
    }
}

/* Generate stars with parallax speeds */
static void generate_stars(GameState *gs)
{
    WORD i;
    ULONG seed = 777777;
    for (i = 0; i < MAX_STARS; i++) {
        seed = seed * 1103515245UL + 12345UL;
        gs->stars[i].x = (WORD)((seed >> 16) % SCREEN_W);
        gs->stars[i].y = (WORD)((seed >> 3) % (SCREEN_H - 30));
        gs->stars[i].accum = 0;
        if (i < 15) {
            gs->stars[i].brightness = COL_WHITE;
            gs->stars[i].speed = 3; /* bright = fastest parallax layer */
        } else if (i < 35) {
            gs->stars[i].brightness = COL_LTGRAY;
            gs->stars[i].speed = 2; /* medium */
        } else {
            gs->stars[i].brightness = COL_SLATE;
            gs->stars[i].speed = 1; /* dim = slowest */
        }
    }
}

/* Scroll stars leftward with sub-pixel accumulation.
 * Stars only ever move 0 or 1 pixel per frame — no multi-pixel jumps. */
void stars_update(GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_STARS; i++) {
        gs->stars[i].accum += gs->stars[i].speed;
        if (gs->stars[i].accum >= 4) {
            gs->stars[i].accum -= 4;
            gs->stars[i].x--;
            if (gs->stars[i].x < 0)
                gs->stars[i].x += SCREEN_W;
        }
    }
}

/* Generate terrain for current level */
static void generate_terrain(GameState *gs)
{
    WORD x, i;
    WORD y = 200;
    WORD roughness = 2 + gs->level;

    if (roughness > 8) roughness = 8;

    /* Seed RNG based on level */
    rng_state = (ULONG)(gs->level * 31337 + 54321);

    /* Generate random terrain */
    for (x = 0; x < TERRAIN_W; x++) {
        WORD delta = (rng_next() % (roughness * 2 + 1)) - roughness;
        y += delta;
        if (y < TERRAIN_MIN) y = TERRAIN_MIN + 5;
        if (y > TERRAIN_MAX) y = TERRAIN_MAX - 5;
        gs->terrain_y[x] = y;
    }

    /* Place landing pads */
    gs->num_pads = 0;
    {
        /* Number of pads decreases with level */
        WORD num_zones = 4;
        WORD zone_w = TERRAIN_W / num_zones;

        /* Multipliers get harder as level increases */
        static const WORD mult_table[4][4] = {
            {1, 1, 2, 2},   /* level 1-2 */
            {1, 2, 2, 3},   /* level 3-4 */
            {2, 2, 3, 5},   /* level 5-6 */
            {2, 3, 5, 5},   /* level 7+ */
        };
        WORD diff = (gs->level - 1) / 2;
        if (diff > 3) diff = 3;

        for (i = 0; i < num_zones && gs->num_pads < MAX_PADS; i++) {
            WORD mult = mult_table[diff][i];
            WORD width;
            WORD zone_start = i * zone_w + 10;
            WORD pad_x, pad_y, px;

            /* Width based on multiplier */
            switch (mult) {
                case 1: width = 40; break;
                case 2: width = 30; break;
                case 3: width = 22; break;
                default: width = 14; break; /* x5 */
            }

            /* Pick X position within zone */
            pad_x = zone_start + (rng_next() % (zone_w - width - 20));
            if (pad_x < 5) pad_x = 5;
            if (pad_x + width >= TERRAIN_W - 5) pad_x = TERRAIN_W - width - 5;

            /* Use average height at pad location */
            pad_y = gs->terrain_y[pad_x + width / 2];
            if (pad_y < TERRAIN_MIN + 10) pad_y = TERRAIN_MIN + 10;
            if (pad_y > TERRAIN_MAX - 5) pad_y = TERRAIN_MAX - 5;

            /* Flatten terrain at pad */
            for (px = pad_x; px < pad_x + width && px < TERRAIN_W; px++) {
                gs->terrain_y[px] = pad_y;
            }

            /* Smooth transitions at pad edges */
            if (pad_x > 2) {
                WORD diff_l = gs->terrain_y[pad_x - 1] - pad_y;
                if (diff_l > 3 || diff_l < -3) {
                    gs->terrain_y[pad_x - 1] = pad_y + diff_l / 2;
                }
            }
            if (pad_x + width < TERRAIN_W - 2) {
                WORD diff_r = gs->terrain_y[pad_x + width] - pad_y;
                if (diff_r > 3 || diff_r < -3) {
                    gs->terrain_y[pad_x + width] = pad_y + diff_r / 2;
                }
            }

            gs->pads[gs->num_pads].x = pad_x;
            gs->pads[gs->num_pads].width = width;
            gs->pads[gs->num_pads].y = pad_y;
            gs->pads[gs->num_pads].multiplier = mult;
            gs->num_pads++;
        }
    }
}

/* Reset ship to starting position */
static void reset_ship(GameState *gs)
{
    gs->ship.x = FIX(SCREEN_W / 2);
    gs->ship.y = FIX(20);
    gs->ship.vx = 0;
    gs->ship.vy = 0;
    gs->ship.angle = 0;
    gs->ship.fuel = (WORD)g_tune.fuel_max;
    gs->ship.alive = 1;
    gs->ship.thrusting = 0;
}

/* Spawn explosion particles */
static void spawn_explosion(GameState *gs, Fixed x, Fixed y)
{
    WORD i;
    for (i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &gs->particles[i];
        if (p->active) continue;
        p->x = x;
        p->y = y;
        p->vx = (Fixed)(rng_next() % 16000 - 8000) << 2;
        p->vy = (Fixed)(rng_next() % 16000 - 12000) << 2;
        p->life = 20 + (rng_next() % 30);
        p->color = (rng_next() & 3) == 0 ? COL_RED :
                   (rng_next() & 1) ? COL_ORANGE : COL_YELLOW;
        p->active = 1;
    }
}

/* Update particles */
static void update_particles(GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &gs->particles[i];
        if (!p->active) continue;
        p->x += p->vx;
        p->y += p->vy;
        p->vy += 600; /* particle gravity */
        p->life--;
        if (p->life <= 0) p->active = 0;
    }
}

/* High score I/O */
static void hiscore_defaults(GameState *gs)
{
    WORD i;
    static const char *names[] = {"ACE","ZAP","BOB","JOE","TOM",
                                  "SAM","MAX","REX","JAK","ROY"};
    for (i = 0; i < MAX_HISCORES; i++) {
        strncpy(gs->hiscores[i].name, names[i], 3);
        gs->hiscores[i].name[3] = '\0';
        gs->hiscores[i].score = (LONG)(MAX_HISCORES - i) * 100L;
    }
}

void hiscore_load(GameState *gs)
{
    BPTR fh;
    hiscore_defaults(gs);

    fh = Open("DH2:Dev/uranus.scores", MODE_OLDFILE);
    if (fh) {
        Read(fh, gs->hiscores, (LONG)sizeof(HiScoreEntry) * MAX_HISCORES);
        Close(fh);
    }
}

void hiscore_save(GameState *gs)
{
    BPTR fh;
    fh = Open("DH2:Dev/uranus.scores", MODE_NEWFILE);
    if (fh) {
        Write(fh, gs->hiscores, (LONG)sizeof(HiScoreEntry) * MAX_HISCORES);
        Close(fh);
    }
}

WORD hiscore_qualifies(GameState *gs)
{
    return (gs->score > gs->hiscores[MAX_HISCORES - 1].score) ? 1 : 0;
}

static void hiscore_insert(GameState *gs)
{
    WORD i, pos;

    /* Find position */
    pos = MAX_HISCORES;
    for (i = 0; i < MAX_HISCORES; i++) {
        if (gs->score > gs->hiscores[i].score) {
            pos = i;
            break;
        }
    }

    if (pos >= MAX_HISCORES) return;

    /* Shift entries down */
    for (i = MAX_HISCORES - 1; i > pos; i--) {
        gs->hiscores[i] = gs->hiscores[i - 1];
    }

    /* Insert new entry */
    strncpy(gs->hiscores[pos].name, gs->name_buf, 3);
    gs->hiscores[pos].name[3] = '\0';
    gs->hiscores[pos].score = gs->score;

    hiscore_save(gs);
}

/* Game initialization */
void game_init(GameState *gs)
{
    WORD i;

    memset(gs, 0, sizeof(GameState));
    gs->lives = (WORD)g_tune.start_lives;
    gs->level = 1;
    gs->score = 0;
    gs->state = STATE_PLAYING;

    /* Clear particles */
    for (i = 0; i < MAX_PARTICLES; i++)
        gs->particles[i].active = 0;

    generate_stars(gs);
    generate_terrain(gs);
    reset_ship(gs);

    /* Init name buffer */
    gs->name_buf[0] = 'A';
    gs->name_buf[1] = 'A';
    gs->name_buf[2] = 'A';
    gs->name_buf[3] = '\0';
    gs->name_pos = 0;
    gs->name_letter = 0;

    hiscore_load(gs);
}

void game_new_level(GameState *gs)
{
    WORD i;
    gs->level++;

    /* Increase difficulty */
    g_tune.gravity = GRAVITY_DEFAULT + (gs->level - 1) * 150L;
    g_tune.fuel_max = FUEL_DEFAULT - (gs->level - 1) * 30L;
    if (g_tune.fuel_max < 200) g_tune.fuel_max = 200;

    /* Clear particles */
    for (i = 0; i < MAX_PARTICLES; i++)
        gs->particles[i].active = 0;

    generate_terrain(gs);
    reset_ship(gs);
}

/* Check landing on terrain/pad */
static void check_collision(GameState *gs)
{
    WORD sx = FIX_INT(gs->ship.x);
    WORD sy = FIX_INT(gs->ship.y);
    WORD ship_bottom = sy + 6; /* ship extends ~6px below center */
    WORD terrain_h;
    WORD i, on_pad;
    WORD pad_idx;
    Fixed abs_vx, abs_vy;
    WORD angle;

    if (sx < 0 || sx >= TERRAIN_W) return;

    terrain_h = gs->terrain_y[sx];

    if (ship_bottom < terrain_h) return; /* still airborne */

    /* Hit the ground - check if on a pad */
    on_pad = 0;
    pad_idx = -1;
    for (i = 0; i < gs->num_pads; i++) {
        if (sx >= gs->pads[i].x && sx < gs->pads[i].x + gs->pads[i].width) {
            on_pad = 1;
            pad_idx = i;
            break;
        }
    }

    if (!on_pad) {
        /* Hit rough terrain - crash! */
        gs->ship.alive = 0;
        gs->state = STATE_CRASHING;
        gs->state_timer = 80;
        gs->ev_crash = 1;
        spawn_explosion(gs, gs->ship.x, gs->ship.y);
        return;
    }

    /* On a pad - check landing conditions */
    abs_vx = gs->ship.vx < 0 ? -gs->ship.vx : gs->ship.vx;
    abs_vy = gs->ship.vy < 0 ? -gs->ship.vy : gs->ship.vy;
    angle = gs->ship.angle;
    if (angle > 128) angle = 256 - angle;

    if (gs->ship.vy < 0 ||
        abs_vy > g_tune.safe_vy ||
        abs_vx > g_tune.safe_vx ||
        angle > g_tune.safe_angle) {
        /* Bad landing - crash! */
        gs->ship.alive = 0;
        gs->state = STATE_CRASHING;
        gs->state_timer = 80;
        gs->ev_crash = 1;
        spawn_explosion(gs, gs->ship.x, gs->ship.y);
        return;
    }

    /* Successful landing! */
    gs->landed_pad = pad_idx;
    gs->land_bonus = (LONG)gs->pads[pad_idx].multiplier * 50L +
                     (LONG)gs->ship.fuel;
    gs->score += gs->land_bonus;
    gs->ship.vx = 0;
    gs->ship.vy = 0;
    gs->ship.thrusting = 0;
    gs->state = STATE_LANDED;
    gs->state_timer = 100;
    gs->ev_land = 1;
}

/* Main game state update */
void game_update(GameState *gs, InputState *inp)
{
    /* Clear sound events */
    gs->ev_thrust = 0;
    gs->ev_crash = 0;
    gs->ev_land = 0;
    gs->ev_low_fuel = 0;

    switch (gs->state) {
    case STATE_PLAYING:
    {
        Ship *s = &gs->ship;

        /* Rotation */
        if (inp->left)  s->angle = (s->angle - (WORD)g_tune.turn_rate) & 255;
        if (inp->right) s->angle = (s->angle + (WORD)g_tune.turn_rate) & 255;

        /* Thrust */
        s->thrusting = 0;
        if ((inp->thrust || inp->up) && s->fuel > 0) {
            Fixed thrust_x = (Fixed)((sin_tab[s->angle] * g_tune.thrust) >> 16);
            Fixed thrust_y = (Fixed)((-cos_tab[s->angle] * g_tune.thrust) >> 16);
            s->vx += thrust_x;
            s->vy += thrust_y;
            s->fuel--;
            s->thrusting = 1;
            gs->ev_thrust = 1;
        }

        /* Gravity (always pulls down = positive Y) */
        s->vy += (Fixed)g_tune.gravity;

        /* Update position */
        s->x += s->vx;
        s->y += s->vy;

        /* Clamp X to screen bounds */
        if (s->x < FIX(5)) { s->x = FIX(5); s->vx = 0; }
        if (s->x > FIX(SCREEN_W - 5)) { s->x = FIX(SCREEN_W - 5); s->vx = 0; }

        /* If ship goes too high, let it (but clamp for sanity) */
        if (s->y < FIX(-40)) s->y = FIX(-40);

        /* If ship goes below screen, crash */
        if (FIX_INT(s->y) > SCREEN_H) {
            s->alive = 0;
            gs->state = STATE_CRASHING;
            gs->state_timer = 60;
            gs->ev_crash = 1;
        }

        /* Check terrain collision */
        if (s->alive)
            check_collision(gs);

        /* Low fuel warning */
        if (s->fuel > 0 && s->fuel < 80 && (gs->frame & 31) == 0)
            gs->ev_low_fuel = 1;

        break;
    }

    case STATE_LANDED:
        gs->state_timer--;
        if (gs->state_timer <= 0) {
            game_new_level(gs);
            gs->state = STATE_PLAYING;
        }
        break;

    case STATE_CRASHING:
        update_particles(gs);
        gs->state_timer--;
        if (gs->state_timer <= 0) {
            gs->lives--;
            if (gs->lives <= 0) {
                gs->state = STATE_GAMEOVER;
                gs->state_timer = 60; /* delay before accepting input */
            } else {
                reset_ship(gs);
                gs->state = STATE_PLAYING;
                gs->state_timer = 50; /* brief invulnerability */
            }
        }
        break;

    case STATE_GAMEOVER:
        update_particles(gs);
        if (gs->state_timer > 0) {
            gs->state_timer--;
        } else if (inp->thrust || inp->up) {
            if (hiscore_qualifies(gs)) {
                gs->name_pos = 0;
                gs->name_buf[0] = 'A';
                gs->name_buf[1] = 'A';
                gs->name_buf[2] = 'A';
                gs->name_buf[3] = '\0';
                gs->name_letter = 0;
                gs->state = STATE_ENTER_NAME;
            } else {
                gs->state = STATE_TITLE;
            }
        }
        break;

    case STATE_ENTER_NAME:
    {
        /* Debounce - only respond every few frames */
        if ((gs->frame & 7) == 0) {
            if (inp->up) {
                /* Previous letter */
                if (gs->name_buf[gs->name_pos] <= 'A')
                    gs->name_buf[gs->name_pos] = 'Z';
                else
                    gs->name_buf[gs->name_pos]--;
            }
            if (inp->down) {
                /* Next letter */
                if (gs->name_buf[gs->name_pos] >= 'Z')
                    gs->name_buf[gs->name_pos] = 'A';
                else
                    gs->name_buf[gs->name_pos]++;
            }
        }

        /* Fire confirms letter */
        if ((inp->thrust) && gs->state_timer == 0) {
            gs->name_pos++;
            gs->state_timer = 10; /* brief delay */
            if (gs->name_pos >= 3) {
                /* Done - insert score and return to title */
                hiscore_insert(gs);
                gs->state = STATE_TITLE;
            }
        }
        if (gs->state_timer > 0) gs->state_timer--;
        break;
    }

    case STATE_TITLE:
        /* Handled in main loop */
        break;
    }

    /* Always update particles */
    if (gs->state == STATE_PLAYING || gs->state == STATE_LANDED)
        update_particles(gs);

    gs->frame++;
}
