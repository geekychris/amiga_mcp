/*
 * game.c - Game logic: player movement, guests, items, room mechanics
 */
#include <proto/dos.h>
#include <proto/exec.h>
#include <exec/memory.h>
#include <string.h>
#include "game.h"

/* Sine table */
BYTE sin_table[256];

/* RNG */
static ULONG game_rng = 12345;
static WORD rng(void)
{
    game_rng = game_rng * 1103515245UL + 12345UL;
    return (WORD)((game_rng >> 16) & 0x7FFF);
}

static WORD rng_range(WORD lo, WORD hi)
{
    return lo + (rng() % (hi - lo + 1));
}

/* Guest colors (palette indices) */
static const WORD guest_colors[] = {
    COL_RED, COL_GREEN, COL_BLUE, COL_ORANGE, COL_PINK, COL_LTGREEN,
    COL_YELLOW, COL_LTBLUE
};
#define NUM_GUEST_COLORS 8

void game_init_tables(void)
{
    WORD i;
    for (i = 0; i < 256; i++) {
        /* Simple sine approximation */
        WORD angle = i;
        WORD val;
        if (angle < 64)
            val = angle * 2;
        else if (angle < 128)
            val = (128 - angle) * 2;
        else if (angle < 192)
            val = -(angle - 128) * 2;
        else
            val = -(256 - angle) * 2;
        if (val > 127) val = 127;
        if (val < -127) val = -127;
        sin_table[i] = (BYTE)val;
    }
}

void game_init(GameState *gs)
{
    WORD i;

    /* Clear everything */
    gs->player.x = 160;
    gs->player.y = FLOOR_Y;
    gs->player.dir = DIR_RIGHT;
    gs->player.frame = 0;
    gs->player.anim_tick = 0;
    gs->player.carrying = ITEM_NONE;
    gs->player.speed = PLAYER_SPEED;
    gs->player.lane = 1;

    for (i = 0; i < MAX_GUESTS; i++)
        gs->guests[i].active = 0;
    for (i = 0; i < MAX_ITEMS; i++)
        gs->items[i].active = 0;

    gs->score = 0;
    gs->lives = START_LIVES;
    gs->party_clock = 0;
    gs->state = GS_PLAYING;

    gs->camera_x = 0;
    gs->camera_target = 0;
    gs->current_room = ROOM_FOYER;

    gs->guest_timer = 100;  /* first guest arrives soon */
    gs->wave = 0;

    gs->happiness = 50;
    gs->flash_timer = 0;
    gs->msg_timer = 0;
    gs->egg_timer = 0;
    gs->tapper_active = 0;
    gs->finale = 0;
    gs->rj_bubble_timer = 0;
    gs->rj_bubble[0] = 0;
    gs->arcade_timer = 0;
    gs->arcade_which = 0;
    {
        WORD pi;
        for (pi = 0; pi < MAX_PUFFS; pi++) gs->puffs[pi].life = 0;
        for (pi = 0; pi < MAX_COPS; pi++) gs->cops[pi].active = 0;
    }
    gs->cop_spawn_timer = 300;
    gs->cop_hit_cooldown = 0;
    gs->plants_collected = 0;

    gs->title_blink = 0;
    gs->credits_scroll = 0;

    game_set_message(gs, "LET'S PARTY!", 100);
}

void game_set_message(GameState *gs, const char *text, WORD duration)
{
    WORD i;
    for (i = 0; i < 39 && text[i]; i++)
        gs->msg[i] = text[i];
    gs->msg[i] = 0;
    gs->msg_timer = duration;
}

/* --- High Score Table --- */

void game_load_hiscores(GameState *gs, const char *filename)
{
    BPTR fh;
    char buf[256];
    LONG len;
    WORD count = 0;
    WORD i;

    /* Init with defaults */
    for (i = 0; i < MAX_HISCORES; i++) {
        strcpy(gs->hi_names[i], "RJ");
        gs->hi_scores[i] = (MAX_HISCORES - i) * 500;
    }
    gs->hi_count = MAX_HISCORES;

    fh = Open((CONST_STRPTR)filename, MODE_OLDFILE);
    if (!fh) return;

    len = Read(fh, buf, 255);
    Close(fh);
    if (len <= 0) return;
    buf[len] = 0;

    /* Parse: one entry per line, "NAME SCORE" */
    count = 0;
    {
        char *p = buf;
        while (*p && count < MAX_HISCORES) {
            WORD ni = 0;
            LONG sc = 0;

            while (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t') p++;
            if (!*p) break;

            /* Read name until space */
            while (*p && *p != ' ' && *p != '\t' && ni < HISCORE_NAMELEN - 1) {
                gs->hi_names[count][ni++] = *p++;
            }
            gs->hi_names[count][ni] = 0;

            /* Skip spaces */
            while (*p == ' ' || *p == '\t') p++;

            /* Read score */
            while (*p >= '0' && *p <= '9') {
                sc = sc * 10 + (*p - '0');
                p++;
            }
            gs->hi_scores[count] = sc;
            count++;

            /* Skip to next line */
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
        }
    }
    if (count > 0) gs->hi_count = count;
}

void game_save_hiscores(GameState *gs, const char *filename)
{
    BPTR fh;
    WORD i;
    char line[40];

    fh = Open((CONST_STRPTR)filename, MODE_NEWFILE);
    if (!fh) return;

    for (i = 0; i < gs->hi_count; i++) {
        WORD len = 0;
        WORD j;
        LONG sc = gs->hi_scores[i];
        char digits[12];
        WORD dlen = 0;

        /* Name */
        for (j = 0; gs->hi_names[i][j]; j++)
            line[len++] = gs->hi_names[i][j];
        line[len++] = ' ';

        /* Score as digits */
        if (sc == 0) { digits[dlen++] = '0'; }
        else {
            while (sc > 0) { digits[dlen++] = '0' + (char)(sc % 10); sc /= 10; }
        }
        for (j = dlen - 1; j >= 0; j--)
            line[len++] = digits[j];
        line[len++] = '\n';

        Write(fh, line, len);
    }
    Close(fh);
}

WORD game_check_hiscore(GameState *gs, LONG score)
{
    WORD i;
    for (i = 0; i < gs->hi_count; i++) {
        if (score > gs->hi_scores[i]) return i;
    }
    if (gs->hi_count < MAX_HISCORES) return gs->hi_count;
    return -1;
}

void game_insert_hiscore(GameState *gs, WORD slot, const char *name, LONG score)
{
    WORD i;
    /* Shift entries down */
    if (gs->hi_count < MAX_HISCORES) gs->hi_count++;
    for (i = gs->hi_count - 1; i > slot; i--) {
        strcpy(gs->hi_names[i], gs->hi_names[i - 1]);
        gs->hi_scores[i] = gs->hi_scores[i - 1];
    }
    /* Insert */
    strncpy(gs->hi_names[slot], name, HISCORE_NAMELEN - 1);
    gs->hi_names[slot][HISCORE_NAMELEN - 1] = 0;
    gs->hi_scores[slot] = score;
}

/* --- Save names (append new guest) --- */

void game_save_names(GameState *gs, const char *filename)
{
    BPTR fh;
    WORD i;

    fh = Open((CONST_STRPTR)filename, MODE_NEWFILE);
    if (!fh) return;

    for (i = 0; i < gs->name_count; i++) {
        WORD len = (WORD)strlen(gs->names[i]);
        Write(fh, gs->names[i], len);
        Write(fh, "\n", 1);
    }
    Close(fh);
}

/* Load guest names from file */
void game_load_names(GameState *gs, const char *filename)
{
    BPTR fh;
    char buf[256];
    WORD count = 0;

    fh = Open((CONST_STRPTR)filename, MODE_OLDFILE);
    if (!fh) {
        /* Default names */
        strcpy(gs->names[0], "Chris");
        strcpy(gs->names[1], "Marcel");
        strcpy(gs->names[2], "Dale");
        strcpy(gs->names[3], "Ashley");
        strcpy(gs->names[4], "Kyle");
        strcpy(gs->names[5], "Elo");
        gs->name_count = 6;
        gs->next_name = 0;
        return;
    }

    while (count < MAX_NAMES) {
        LONG len;
        WORD i;

        len = Read(fh, buf, 255);
        if (len <= 0) break;
        buf[len] = 0;

        /* Parse line by line from buffer */
        {
            char *p = buf;
            while (*p && count < MAX_NAMES) {
                /* Skip whitespace */
                while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
                if (!*p) break;

                /* Read name */
                i = 0;
                while (*p && *p != '\r' && *p != '\n' && i < NAME_LEN - 1) {
                    gs->names[count][i++] = *p++;
                }
                gs->names[count][i] = 0;
                if (i > 0) count++;
            }
        }
        /* Only read one chunk - simple parser */
        break;
    }

    Close(fh);

    if (count == 0) {
        strcpy(gs->names[0], "Friend");
        count = 1;
    }
    gs->name_count = count;
    gs->next_name = 0;
}

/* --- Item management --- */

static Item *spawn_item(GameState *gs, WORD type, WORD x, WORD y,
                         WORD vx, WORD vy, WORD room)
{
    WORD i;
    for (i = 0; i < MAX_ITEMS; i++) {
        if (!gs->items[i].active) {
            gs->items[i].active = 1;
            gs->items[i].type = type;
            gs->items[i].x = x;
            gs->items[i].y = y;
            gs->items[i].vx = vx;
            gs->items[i].vy = vy;
            gs->items[i].room = room;
            gs->items[i].timer = 0;
            return &gs->items[i];
        }
    }
    return NULL;
}

/* --- Guest management --- */

static Guest *spawn_guest(GameState *gs, WORD room, WORD x, WORD y)
{
    WORD i;
    for (i = 0; i < MAX_GUESTS; i++) {
        if (!gs->guests[i].active) {
            Guest *g = &gs->guests[i];
            g->active = 1;
            g->state = GUEST_ENTERING;
            g->x = x;
            g->y = y;
            g->room = room;
            g->name_idx = gs->next_name;
            gs->next_name = (gs->next_name + 1) % gs->name_count;
            g->want = ITEM_NONE;
            g->patience = 0;
            g->patience_max = 0;
            g->color = guest_colors[rng() % NUM_GUEST_COLORS];
            g->lane = rng_range(0, 2);
            g->timer = 0;
            g->target_x = rng_range(room * ROOM_W + 40, room * ROOM_W + 280);
            g->bubble_timer = 0;
            g->greeted = 0;
            g->bubble_text[0] = 0;
            sfx_doorbell();
            return g;
        }
    }
    return NULL;
}

/* --- Smoke puff system --- */

static void spawn_smoke(GameState *gs, WORD x, WORD y)
{
    WORD i;
    /* Spawn a burst of puffs from RJ's head */
    for (i = 0; i < MAX_PUFFS; i++) {
        SmokePuff *p = &gs->puffs[i];
        if (p->life <= 0) {
            p->x = x;
            p->y = y;
            p->vx = rng_range(-2, 2);
            p->vy = rng_range(-3, -1);
            p->life = PUFF_LIFE + rng_range(0, 10);
            p->size = rng_range(2, 5);
            break;
        }
    }
}

static void update_puffs(GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_PUFFS; i++) {
        SmokePuff *p = &gs->puffs[i];
        if (p->life <= 0) continue;
        p->x += p->vx;
        p->y += p->vy;
        /* Drift upward and slow */
        if ((p->life & 3) == 0) {
            if (p->vx > 0) p->vx--;
            else if (p->vx < 0) p->vx++;
        }
        p->life--;
        /* Grow as it fades */
        if ((p->life & 7) == 0 && p->size < 8) p->size++;
    }
}

/* --- Pot Police --- */

static void spawn_cop(GameState *gs)
{
    WORD i;
    WORD room_x = ROOM_AMSTERDAM * ROOM_W;

    for (i = 0; i < MAX_COPS; i++) {
        if (!gs->cops[i].active) {
            PotCop *c = &gs->cops[i];
            c->active = 1;
            /* Spawn at edge of Amsterdam, occasionally wander further */
            if (gs->wave >= 5 && (rng() & 3) == 0) {
                /* Roaming cop - patrols Amsterdam + one neighbor */
                WORD side = rng() & 1;
                c->patrol_min = room_x - (side ? ROOM_W / 2 : 0);
                c->patrol_max = room_x + ROOM_W + (side ? 0 : ROOM_W / 2);
            } else {
                c->patrol_min = room_x;
                c->patrol_max = room_x + ROOM_W - 20;
            }
            c->x = (rng() & 1) ? c->patrol_min + 10 : c->patrol_max - 10;
            c->y = FLOOR_Y;
            c->dir = DIR_RIGHT;
            c->frame = 0;
            c->anim_tick = 0;
            c->chase = 0;
            c->speed = COP_SPEED;
            c->cooldown = 0;
            return;
        }
    }
}

static void update_cops(GameState *gs)
{
    WORD i;
    Player *p = &gs->player;

    /* Decrease hit cooldown */
    if (gs->cop_hit_cooldown > 0) gs->cop_hit_cooldown--;

    /* Spawn cops based on plants collected and wave */
    if (gs->plants_collected > 0) {
        gs->cop_spawn_timer--;
        if (gs->cop_spawn_timer <= 0) {
            spawn_cop(gs);
            /* Spawn faster with more plants and higher wave */
            gs->cop_spawn_timer = 400 - gs->plants_collected * 30 - gs->wave * 20;
            if (gs->cop_spawn_timer < 100) gs->cop_spawn_timer = 100;
        }
    }

    for (i = 0; i < MAX_COPS; i++) {
        PotCop *c = &gs->cops[i];
        WORD dx, dy;
        if (!c->active) continue;

        if (c->cooldown > 0) c->cooldown--;

        /* Check distance to player */
        dx = p->x - c->x;
        dy = p->y - c->y;

        /* Start chasing if player is nearby */
        if (dx > -80 && dx < 80 && dy > -40 && dy < 40) {
            c->chase = 1;
        } else {
            c->chase = 0;
        }

        /* Movement */
        if (c->chase) {
            /* Chase player */
            if (p->x < c->x) {
                c->x -= c->speed + 1;
                c->dir = DIR_LEFT;
            } else {
                c->x += c->speed + 1;
                c->dir = DIR_RIGHT;
            }
            if (p->y < c->y) c->y -= 1;
            else if (p->y > c->y) c->y += 1;
        } else {
            /* Patrol back and forth */
            if (c->dir == DIR_RIGHT) {
                c->x += c->speed;
                if (c->x >= c->patrol_max) {
                    c->dir = DIR_LEFT;
                }
            } else {
                c->x -= c->speed;
                if (c->x <= c->patrol_min) {
                    c->dir = DIR_RIGHT;
                }
            }
        }

        /* Animate */
        c->anim_tick++;
        if (c->anim_tick >= 5) {
            c->anim_tick = 0;
            c->frame ^= 1;
        }

        /* Collision with player */
        if (gs->cop_hit_cooldown == 0 && c->cooldown == 0) {
            if (dx > -12 && dx < 12 && dy > -12 && dy < 12) {
                gs->lives--;
                gs->cop_hit_cooldown = 100; /* invulnerable for 2 seconds */
                c->cooldown = 100;
                sfx_buzzer();
                sfx_crash();
                /* Push player away */
                p->x += (dx > 0) ? -30 : 30;
                /* Funny messages */
                {
                    static const char *cop_msgs[] = {
                        "BUSTED!",
                        "HALT!",
                        "NOT SO FAST!",
                        "PARTY FOUL!",
                        "HANDS UP!"
                    };
                    game_set_message(gs, cop_msgs[rng() % 5], 40);
                }
                gs->rj_bubble_timer = 50;
                {
                    static const char *rj_msgs[] = {
                        "It's medicinal!",
                        "RUN!",
                        "I know my rights!",
                        "Yikes!",
                        "Not cool man!"
                    };
                    const char *rm = rj_msgs[rng() % 5];
                    WORD j;
                    for (j = 0; rm[j] && j < BUBBLE_LEN - 1; j++)
                        gs->rj_bubble[j] = rm[j];
                    gs->rj_bubble[j] = 0;
                }
            }
        }

        /* Despawn if far from Amsterdam and not chasing */
        if (!c->chase && (c->x < ROOM_AMSTERDAM * ROOM_W - ROOM_W ||
                          c->x > ROOM_AMSTERDAM * ROOM_W + ROOM_W * 2)) {
            c->active = 0;
        }
    }
}

/* --- Room-specific update logic --- */

/* Foyer: gifts slide in from right, catch them */
static void update_foyer(GameState *gs, InputState *inp)
{
    WORD i;
    Player *p = &gs->player;

    /* Spawn gifts periodically */
    if ((gs->party_clock % (80 - gs->wave * 5)) == 0 && gs->wave < 10) {
        spawn_item(gs, ITEM_GIFT,
                   ROOM_W - 10,  /* right edge of foyer */
                   rng_range(FLOOR_Y - 8, FLOOR_Y),
                   -2, 0, ROOM_FOYER);
    }

    /* Move gifts left */
    for (i = 0; i < MAX_ITEMS; i++) {
        Item *it = &gs->items[i];
        if (!it->active || it->room != ROOM_FOYER) continue;

        it->x += it->vx;

        /* Player catches gift */
        if (it->type == ITEM_GIFT) {
            WORD dx = p->x - it->x;
            WORD dy = p->y - it->y;
            if (dx > -12 && dx < 12 && dy > -12 && dy < 12) {
                it->active = 0;
                gs->score += 100;
                sfx_pickup();
                game_set_message(gs, "NICE CATCH!", 30);
            }
        }

        /* Gift fell off left edge */
        if (it->x < -10) {
            it->active = 0;
            gs->happiness -= 5;
            sfx_crash();
        }
    }
    (void)inp;
}

/* Amsterdam: Tapper mechanics */
static void update_amsterdam(GameState *gs, InputState *inp)
{
    WORD i;
    Player *p = &gs->player;
    WORD room_x = ROOM_AMSTERDAM * ROOM_W;

    /* Player can serve brownies by pressing fire anywhere in Amsterdam */
    if (inp->fire_edge && p->x >= room_x && p->x < room_x + ROOM_W) {
        /* Determine which lane based on player Y */
        WORD serve_lane = (p->y - TAPPER_LANE_Y0 + TAPPER_LANE_H / 2) / TAPPER_LANE_H;
        if (serve_lane < 0) serve_lane = 0;
        if (serve_lane >= TAPPER_LANES) serve_lane = TAPPER_LANES - 1;
        spawn_item(gs, ITEM_BROWNIE,
                   room_x + 30,
                   TAPPER_LANE_Y0 + serve_lane * TAPPER_LANE_H + 10,
                   4, 0, ROOM_AMSTERDAM);
        sfx_slide();
    }

    /* Move brownies right */
    for (i = 0; i < MAX_ITEMS; i++) {
        Item *it = &gs->items[i];
        if (!it->active || it->room != ROOM_AMSTERDAM) continue;
        if (it->type != ITEM_BROWNIE) continue;

        it->x += it->vx;

        /* Check collision with guests in same lane */
        {
            WORD j;
            for (j = 0; j < MAX_GUESTS; j++) {
                Guest *g = &gs->guests[j];
                if (!g->active || g->room != ROOM_AMSTERDAM) continue;
                if (g->state != GUEST_WANT) continue;

                /* Check if brownie hits guest */
                if (it->x + 8 >= g->x && it->x <= g->x + GUEST_W) {
                    WORD item_lane = (it->y - TAPPER_LANE_Y0 -
                                      ROOM_AMSTERDAM * 0 + 2) / TAPPER_LANE_H;
                    if (item_lane == g->lane) {
                        it->active = 0;
                        g->state = GUEST_HAPPY;
                        g->timer = 60;
                        gs->score += 200;
                        sfx_ding();
                        game_set_message(gs, "ENJOY!", 20);
                    }
                }
            }
        }

        /* Brownie fell off right edge */
        if (it->x > room_x + ROOM_W) {
            it->active = 0;
            sfx_crash();
        }
    }

    /* Spawn guests on right approaching bar - delayed start */
    if (gs->party_clock > 200 && (gs->party_clock % (180 - gs->wave * 8)) == 0) {
        Guest *g = spawn_guest(gs, ROOM_AMSTERDAM,
                               room_x + 300,
                               TAPPER_LANE_Y0 + rng_range(0, TAPPER_LANES - 1) * TAPPER_LANE_H + 14);
        if (g) {
            g->want = ITEM_BROWNIE;
            g->patience = 500 - gs->wave * 20;
            g->patience_max = g->patience;
            g->bubble_timer = g->patience;
            g->lane = (g->y - TAPPER_LANE_Y0) / TAPPER_LANE_H;
            g->target_x = room_x + 50;
        }
    }

    /* Move approaching guests left */
    for (i = 0; i < MAX_GUESTS; i++) {
        Guest *g = &gs->guests[i];
        if (!g->active || g->room != ROOM_AMSTERDAM) continue;

        if (g->state == GUEST_ENTERING) {
            g->x -= 1;
            if (g->x <= g->target_x) {
                g->state = GUEST_WANT;
            }
        }
        else if (g->state == GUEST_WANT) {
            g->patience--;
            if (g->patience <= 0) {
                g->state = GUEST_ANGRY;
                g->timer = 40;
                gs->happiness -= 10;
                sfx_buzzer();
                game_set_message(gs, "CUSTOMER LEFT!", 30);
            }
            /* Slowly approach bar */
            if ((gs->party_clock & 15) == 0) g->x -= 1;
            if (g->x < room_x + 30) {
                g->state = GUEST_ANGRY;
                g->timer = 40;
                gs->happiness -= 10;
                sfx_buzzer();
            }
        }
        else if (g->state == GUEST_HAPPY || g->state == GUEST_ANGRY) {
            g->timer--;
            if (g->timer <= 0) {
                g->state = GUEST_LEAVING;
                g->target_x = room_x + 330;
            }
        }
        else if (g->state == GUEST_LEAVING) {
            g->x += 2;
            if (g->x > room_x + 330) {
                g->active = 0;
            }
        }
    }

    /* Spawn plants around the room */
    if ((gs->party_clock % 150) == 50) {
        spawn_item(gs, ITEM_PLANT,
                   room_x + rng_range(40, 300),
                   rng_range(FLOOR_Y - 15, FLOOR_Y),
                   0, 0, ROOM_AMSTERDAM);
    }

    /* Player picks up plants by walking over them */
    for (i = 0; i < MAX_ITEMS; i++) {
        Item *it = &gs->items[i];
        if (!it->active || it->room != ROOM_AMSTERDAM || it->type != ITEM_PLANT)
            continue;
        {
            WORD dx = p->x - it->x;
            WORD dy = p->y - it->y;
            if (dx > -15 && dx < 15 && dy > -15 && dy < 15) {
                it->active = 0;
                gs->score += 100;
                gs->happiness += 2;
                gs->plants_collected++;
                sfx_pickup();
                /* Spawn smoke puffs from RJ's head */
                {
                    WORD si;
                    for (si = 0; si < 4; si++)
                        spawn_smoke(gs, p->x, p->y - PLAYER_H - 4);
                }
                gs->rj_bubble_timer = 40;
                {
                    static const char *puff_msgs[] = {
                        "Far out!",
                        "Groovy!",
                        "Mellow...",
                        "Nice!",
                        "Whoa dude!"
                    };
                    const char *pm = puff_msgs[rng() % 5];
                    WORD j;
                    for (j = 0; pm[j] && j < BUBBLE_LEN - 1; j++)
                        gs->rj_bubble[j] = pm[j];
                    gs->rj_bubble[j] = 0;
                }
            }
        }
    }
}

/* Tokyo: sushi matching */
static void update_tokyo(GameState *gs, InputState *inp)
{
    WORD i;
    Player *p = &gs->player;
    WORD room_x = ROOM_TOKYO * ROOM_W;
    static const WORD sushi_types[] = { ITEM_SUSHI_R, ITEM_SUSHI_B, ITEM_SUSHI_G };

    /* Spawn sushi on conveyor */
    if ((gs->party_clock % (60 - gs->wave * 3)) == 0) {
        WORD lane = rng_range(0, SUSHI_LANES - 1);
        WORD stype = sushi_types[rng_range(0, 2)];
        spawn_item(gs, stype,
                   room_x - 10,
                   SUSHI_LANE_Y0 + lane * SUSHI_LANE_H + 8,
                   2, 0, ROOM_TOKYO);
    }

    /* Move sushi right */
    for (i = 0; i < MAX_ITEMS; i++) {
        Item *it = &gs->items[i];
        if (!it->active || it->room != ROOM_TOKYO) continue;
        it->x += it->vx;

        /* Player picks up sushi */
        if (p->carrying == ITEM_NONE && inp->fire_edge) {
            WORD dx = p->x - it->x;
            WORD dy = p->y - it->y;
            if (dx > -15 && dx < 15 && dy > -15 && dy < 15) {
                p->carrying = it->type;
                it->active = 0;
                sfx_pickup();
            }
        }

        /* Sushi falls off right edge */
        if (it->x > room_x + ROOM_W + 10) {
            it->active = 0;
        }
    }

    /* Serve sushi to guests */
    if (p->carrying >= ITEM_SUSHI_R && p->carrying <= ITEM_SUSHI_G && inp->fire_edge) {
        for (i = 0; i < MAX_GUESTS; i++) {
            Guest *g = &gs->guests[i];
            WORD dx, dy;
            if (!g->active || g->room != ROOM_TOKYO || g->state != GUEST_WANT) continue;
            dx = p->x - g->x;
            dy = p->y - g->y;
            if (dx > -20 && dx < 20 && dy > -20 && dy < 20) {
                if (p->carrying == g->want) {
                    g->state = GUEST_HAPPY;
                    g->timer = 60;
                    gs->score += 300;
                    sfx_ding();
                    sfx_chomp();
                    game_set_message(gs, "OISHII!", 25);
                } else {
                    g->state = GUEST_ANGRY;
                    g->timer = 40;
                    gs->happiness -= 5;
                    sfx_buzzer();
                    game_set_message(gs, "WRONG SUSHI!", 25);
                }
                p->carrying = ITEM_NONE;
                break;
            }
        }
    }

    /* Spawn guest diners */
    if ((gs->party_clock % (150 - gs->wave * 10)) == 0) {
        Guest *g = spawn_guest(gs, ROOM_TOKYO,
                               room_x + rng_range(200, 290),
                               SUSHI_LANE_Y0 + rng_range(0, SUSHI_LANES - 1) * SUSHI_LANE_H + 14);
        if (g) {
            g->want = sushi_types[rng_range(0, 2)];
            g->patience = 400 - gs->wave * 20;
            g->patience_max = g->patience;
            g->bubble_timer = g->patience;
            g->state = GUEST_WANT;
        }
    }

    /* Update guest patience */
    for (i = 0; i < MAX_GUESTS; i++) {
        Guest *g = &gs->guests[i];
        if (!g->active || g->room != ROOM_TOKYO) continue;
        if (g->state == GUEST_WANT) {
            g->patience--;
            if (g->patience <= 0) {
                g->state = GUEST_ANGRY;
                g->timer = 40;
                gs->happiness -= 8;
                sfx_buzzer();
            }
        }
        else if (g->state == GUEST_HAPPY || g->state == GUEST_ANGRY) {
            g->timer--;
            if (g->timer <= 0) g->active = 0;
        }
    }
}

/* Silicon Valley: demo station interactions */
static void update_silicon(GameState *gs, InputState *inp)
{
    WORD i;
    Player *p = &gs->player;
    WORD room_x = ROOM_SILICON * ROOM_W;

    /* Demo stations: Amiga at x=155, Lynx at x=215, 3DO at x=275 */
    static const WORD station_x[] = { 155, 215, 275 };
    static const char *station_names[] = { "AMIGA", "LYNX", "3DO" };

    /* Check if player is near a station and presses fire */
    if (inp->fire_edge) {
        WORD s;
        for (s = 0; s < 3; s++) {
            WORD sx = room_x + station_x[s];
            WORD dx = p->x - sx;
            if (dx > -20 && dx < 20 && p->y > FLOOR_Y - 50 && p->y < FLOOR_Y) {
                /* Check if a guest is nearby wanting a demo */
                for (i = 0; i < MAX_GUESTS; i++) {
                    Guest *g = &gs->guests[i];
                    WORD gdx;
                    if (!g->active || g->room != ROOM_SILICON || g->state != GUEST_WANT)
                        continue;
                    gdx = g->x - sx;
                    if (gdx > -30 && gdx < 30) {
                        g->state = GUEST_HAPPY;
                        g->timer = 80;
                        gs->score += 250;
                        sfx_cheer();
                        game_set_message(gs, station_names[s], 30);

                        /* Easter egg: Intuition reference at Amiga station */
                        if (s == 0 && gs->egg_timer <= 0) {
                            WORD j;
                            for (j = 0; EGG_INTUITION[j] && j < 39; j++)
                                gs->egg_text[j] = EGG_INTUITION[j];
                            gs->egg_text[j] = 0;
                            gs->egg_timer = 100;
                        }
                        break;
                    }
                }
            }
        }
    }

    /* Spawn tech enthusiast guests */
    if ((gs->party_clock % (130 - gs->wave * 8)) == 0) {
        WORD station = rng_range(0, 2);
        Guest *g = spawn_guest(gs, ROOM_SILICON,
                               room_x + 310,
                               FLOOR_Y);
        if (g) {
            g->want = ITEM_DEMO;
            g->patience = 350 - gs->wave * 15;
            g->patience_max = g->patience;
            g->bubble_timer = g->patience;
            g->target_x = room_x + station_x[station];
        }
    }

    /* Move guests toward stations */
    for (i = 0; i < MAX_GUESTS; i++) {
        Guest *g = &gs->guests[i];
        if (!g->active || g->room != ROOM_SILICON) continue;

        if (g->state == GUEST_ENTERING) {
            if (g->x > g->target_x + 5) g->x -= 2;
            else if (g->x < g->target_x - 5) g->x += 2;
            else g->state = GUEST_WANT;
        }
        else if (g->state == GUEST_WANT) {
            g->patience--;
            if (g->patience <= 0) {
                g->state = GUEST_ANGRY;
                g->timer = 40;
                gs->happiness -= 8;
                sfx_buzzer();
            }
        }
        else if (g->state == GUEST_HAPPY || g->state == GUEST_ANGRY) {
            g->timer--;
            if (g->timer <= 0) {
                g->state = GUEST_LEAVING;
                g->target_x = room_x + 330;
            }
        }
        else if (g->state == GUEST_LEAVING) {
            g->x += 2;
            if (g->x > room_x + 330) g->active = 0;
        }
    }
}

/* Bay Area: dodge scooters, deliver favors */
static void update_bayarea(GameState *gs, InputState *inp)
{
    WORD i;
    Player *p = &gs->player;
    WORD room_x = ROOM_BAYAREA * ROOM_W;

    /* Spawn scooters (obstacles) */
    if ((gs->party_clock % (40 - gs->wave * 2)) == 0) {
        WORD dir = rng() & 1;
        spawn_item(gs, ITEM_FAVOR, /* reuse type for scooter obstacle */
                   dir ? room_x - 10 : room_x + 330,
                   FLOOR_Y - 20,
                   dir ? 5 : -5, 0, ROOM_BAYAREA);
    }

    /* Spawn party favor pickups */
    if ((gs->party_clock % 200) == 0) {
        spawn_item(gs, ITEM_FAVOR,
                   room_x + rng_range(30, 290),
                   FLOOR_Y - 10,
                   0, 0, ROOM_BAYAREA);
    }

    /* Move items, check collisions */
    for (i = 0; i < MAX_ITEMS; i++) {
        Item *it = &gs->items[i];
        if (!it->active || it->room != ROOM_BAYAREA) continue;

        if (it->vx != 0) {
            /* Moving obstacle (scooter) */
            it->x += it->vx;

            /* Hit player? */
            {
                WORD dx = p->x - it->x;
                WORD dy = p->y - it->y;
                if (dx > -10 && dx < 10 && dy > -10 && dy < 10) {
                    gs->lives--;
                    gs->happiness -= 10;
                    sfx_crash();
                    game_set_message(gs, "SCOOTER HIT!", 30);
                    /* Push player back */
                    p->x += (it->vx > 0) ? 20 : -20;
                    it->active = 0;
                }
            }

            /* Off screen */
            if (it->x < room_x - 20 || it->x > room_x + 340)
                it->active = 0;
        } else {
            /* Stationary pickup */
            if (inp->fire_edge && p->carrying == ITEM_NONE) {
                WORD dx = p->x - it->x;
                WORD dy = p->y - it->y;
                if (dx > -15 && dx < 15 && dy > -15 && dy < 15) {
                    p->carrying = ITEM_FAVOR;
                    it->active = 0;
                    sfx_pickup();
                }
            }
        }
    }

    /* Deliver favors to guests */
    if (p->carrying == ITEM_FAVOR && inp->fire_edge) {
        for (i = 0; i < MAX_GUESTS; i++) {
            Guest *g = &gs->guests[i];
            WORD dx, dy;
            if (!g->active || g->room != ROOM_BAYAREA || g->state != GUEST_WANT) continue;
            dx = p->x - g->x;
            dy = p->y - g->y;
            if (dx > -20 && dx < 20 && dy > -20 && dy < 20) {
                g->state = GUEST_HAPPY;
                g->timer = 60;
                gs->score += 200;
                p->carrying = ITEM_NONE;
                sfx_ding();
                game_set_message(gs, "PARTY ON!", 20);
                break;
            }
        }
    }

    /* Spawn patio guests */
    if ((gs->party_clock % (160 - gs->wave * 10)) == 0) {
        Guest *g = spawn_guest(gs, ROOM_BAYAREA,
                               room_x + rng_range(50, 280),
                               FLOOR_Y);
        if (g) {
            g->want = ITEM_FAVOR;
            g->patience = 350 - gs->wave * 15;
            g->patience_max = g->patience;
            g->bubble_timer = g->patience;
            g->state = GUEST_WANT;
        }
    }

    /* Update guest patience */
    for (i = 0; i < MAX_GUESTS; i++) {
        Guest *g = &gs->guests[i];
        if (!g->active || g->room != ROOM_BAYAREA) continue;
        if (g->state == GUEST_WANT) {
            g->patience--;
            if (g->patience <= 0) {
                g->state = GUEST_ANGRY;
                g->timer = 40;
                gs->happiness -= 8;
                sfx_buzzer();
            }
        }
        else if (g->state == GUEST_HAPPY || g->state == GUEST_ANGRY) {
            g->timer--;
            if (g->timer <= 0) g->active = 0;
        }
    }

    (void)inp;
}

/* Living Room: finale / dance party */
static void update_living(GameState *gs, InputState *inp)
{
    Player *p = &gs->player;
    WORD room_x = ROOM_LIVING * ROOM_W;

    /* Check if party time is almost over -> trigger finale */
    if (gs->party_clock > PARTY_FRAMES - 200 && !gs->finale) {
        gs->finale = 1;
        game_set_message(gs, "GET TO THE CAKE!", 100);
    }

    /* Cake is at center of living room */
    if (gs->finale && inp->fire_edge) {
        WORD cake_x = room_x + 160;
        WORD dx = p->x - cake_x;
        if (dx > -25 && dx < 25 && p->y > FLOOR_Y - 40 && p->y < FLOOR_Y + 10) {
            /* BLOW OUT CANDLES - WIN! */
            gs->state = GS_WIN;
            gs->score += 1000;
            sfx_cheer();
            sfx_party();
        }
    }

    /* Spawn dancing guests */
    if ((gs->party_clock % 200) == 0) {
        Guest *g = spawn_guest(gs, ROOM_LIVING,
                               room_x + rng_range(30, 290),
                               FLOOR_Y);
        if (g) {
            g->state = GUEST_HAPPY;
            g->timer = 300;
        }
    }

    (void)inp;
}

/* --- Main update --- */

static void update_player(GameState *gs, InputState *inp)
{
    Player *p = &gs->player;
    WORD moving = 0;
    WORD room = gs->current_room;

    /* Amsterdam: free movement, fire to serve from anywhere */
    (void)room;

    /* Movement */
    if (inp->bits & INP_LEFT) {
        p->x -= p->speed;
        p->dir = DIR_LEFT;
        moving = 1;
    }
    if (inp->bits & INP_RIGHT) {
        p->x += p->speed;
        p->dir = DIR_RIGHT;
        moving = 1;
    }
    if (inp->bits & INP_UP) {
        p->y -= p->speed;
        moving = 1;
    }
    if (inp->bits & INP_DOWN) {
        p->y += p->speed;
        moving = 1;
    }

    /* Clamp Y */
    if (p->y < WALK_Y_MIN) p->y = WALK_Y_MIN;
    if (p->y > WALK_Y_MAX) p->y = WALK_Y_MAX;

    /* Clamp X to world bounds */
    if (p->x < 10) p->x = 10;
    if (p->x > WORLD_W - 10) p->x = WORLD_W - 10;

    /* Animation */
    if (moving) {
        p->anim_tick++;
        if (p->anim_tick >= 6) {
            p->anim_tick = 0;
            p->frame ^= 1;
        }
    } else {
        p->frame = 0;
        p->anim_tick = 0;
    }

    /* Determine current room */
    gs->current_room = p->x / ROOM_W;
    if (gs->current_room >= ROOM_COUNT) gs->current_room = ROOM_COUNT - 1;
}

static void update_camera(GameState *gs)
{
    /* Camera follows player, centered */
    WORD target = gs->player.x - SCREEN_W / 2;

    /* Clamp */
    if (target < 0) target = 0;
    if (target > WORLD_W - SCREEN_W) target = WORLD_W - SCREEN_W;

    gs->camera_target = target;

    /* Smooth lerp */
    if (gs->camera_x < gs->camera_target) {
        gs->camera_x += (gs->camera_target - gs->camera_x + 3) / 4;
        if (gs->camera_x > gs->camera_target) gs->camera_x = gs->camera_target;
    }
    else if (gs->camera_x > gs->camera_target) {
        gs->camera_x -= (gs->camera_x - gs->camera_target + 3) / 4;
        if (gs->camera_x < gs->camera_target) gs->camera_x = gs->camera_target;
    }
}

void game_update(GameState *gs, InputState *inp)
{
    /* Title screen */
    if (gs->state == GS_TITLE) {
        gs->title_blink++;
        if (inp->fire_edge) {
            game_init(gs);
        }
        /* H = help */
        if (inp->last_char == 'h') {
            gs->state = GS_HELP;
            gs->help_scroll = 0;
        }
        /* E = guest list editor */
        if (inp->last_char == 'e') {
            gs->edit_return_state = GS_TITLE;
            gs->state = GS_GUEST_EDIT;
            gs->edit_cursor = 0;
            gs->edit_scroll = 0;
        }
        return;
    }

    /* Help screen */
    if (gs->state == GS_HELP) {
        gs->help_scroll++;
        if (inp->fire_edge || (inp->bits & INP_ESC)) {
            gs->state = GS_TITLE;
        }
        return;
    }

    /* Guest list editor */
    if (gs->state == GS_GUEST_EDIT) {
        gs->entry_blink++;

        /* Navigate - use edge detect with repeat delay */
        {
            static WORD nav_cooldown = 0;
            if (nav_cooldown > 0) nav_cooldown--;

            if ((inp->key_up || ((inp->bits & INP_UP) && nav_cooldown == 0))
                && gs->edit_cursor > 0) {
                gs->edit_cursor--;
                sfx_pickup();
                nav_cooldown = 8;
            }
            if ((inp->key_down || ((inp->bits & INP_DOWN) && nav_cooldown == 0))
                && gs->edit_cursor < gs->name_count) {
                gs->edit_cursor++;
                sfx_pickup();
                nav_cooldown = 8;
            }
            if (!(inp->bits & (INP_UP | INP_DOWN))) nav_cooldown = 0;
        }

        /* Delete selected name */
        if (inp->key_delete && gs->edit_cursor < gs->name_count && gs->name_count > 1) {
            WORD j;
            for (j = gs->edit_cursor; j < gs->name_count - 1; j++) {
                strcpy(gs->names[j], gs->names[j + 1]);
            }
            gs->name_count--;
            if (gs->edit_cursor >= gs->name_count)
                gs->edit_cursor = gs->name_count - 1;
            sfx_buzzer();
        }

        /* Add new name: cursor at bottom = add slot */
        if (inp->key_return) {
            if (gs->edit_cursor >= gs->name_count && gs->name_count < MAX_NAMES) {
                /* Enter add-name mode */
                gs->state = GS_ADD_GUEST;
                gs->entry_pos = 0;
                gs->entry_name[0] = 0;
                gs->entry_mode = 1;
                gs->entry_blink = 0;
            }
        }

        /* ESC = save and return */
        if (inp->bits & INP_ESC) {
            game_save_names(gs, "PROGDIR:guests.txt");
            gs->state = gs->edit_return_state;
            sfx_ding();
        }

        return;
    }

    /* Game over / win -> check high score */
    if (gs->state == GS_GAMEOVER || gs->state == GS_WIN) {
        if (gs->flash_timer > 0) { gs->flash_timer--; return; }
        if (inp->fire_edge) {
            WORD slot = game_check_hiscore(gs, gs->score);
            if (slot >= 0) {
                /* New high score! Enter name */
                gs->state = GS_ENTER_NAME;
                gs->entry_pos = 0;
                gs->entry_name[0] = 0;
                gs->entry_char = 0;
                gs->entry_mode = 0;  /* high score entry */
                gs->entry_blink = 0;
            } else {
                gs->state = GS_HISCORE;
                gs->flash_timer = 30;
            }
        }
        return;
    }

    /* High score display */
    if (gs->state == GS_HISCORE) {
        if (gs->flash_timer > 0) { gs->flash_timer--; return; }
        if (inp->fire_edge) {
            /* Offer to add name to guest list */
            gs->state = GS_ADD_GUEST;
            gs->entry_pos = 0;
            gs->entry_name[0] = 0;
            gs->entry_char = 0;
            gs->entry_mode = 2;  /* from hiscore flow */
            gs->entry_blink = 0;
        }
        return;
    }

    /* Name entry (for high score or adding guest) */
    if (gs->state == GS_ENTER_NAME || gs->state == GS_ADD_GUEST) {
        gs->entry_blink++;

        /* Type a character */
        if (inp->last_char && gs->entry_pos < HISCORE_NAMELEN - 1) {
            UBYTE ch = inp->last_char;
            /* Uppercase it */
            if (ch >= 'a' && ch <= 'z') ch -= 32;
            gs->entry_name[gs->entry_pos++] = ch;
            gs->entry_name[gs->entry_pos] = 0;
            sfx_pickup();
        }

        /* Backspace */
        if (inp->key_backspace && gs->entry_pos > 0) {
            gs->entry_pos--;
            gs->entry_name[gs->entry_pos] = 0;
        }

        /* Return = confirm */
        if (inp->key_return && gs->entry_pos > 0) {
            if (gs->state == GS_ENTER_NAME) {
                /* Insert high score */
                WORD slot = game_check_hiscore(gs, gs->score);
                if (slot >= 0) {
                    game_insert_hiscore(gs, slot, gs->entry_name, gs->score);
                    game_save_hiscores(gs, "PROGDIR:hiscores.dat");
                }
                gs->state = GS_HISCORE;
                gs->flash_timer = 30;
                sfx_cheer();
            } else {
                /* Add guest to names list */
                if (gs->name_count < MAX_NAMES) {
                    strncpy(gs->names[gs->name_count], gs->entry_name, NAME_LEN - 1);
                    gs->names[gs->name_count][NAME_LEN - 1] = 0;
                    gs->name_count++;
                    game_save_names(gs, "PROGDIR:guests.txt");
                    sfx_cheer();
                }
                /* Return to editor if we came from there, otherwise credits */
                if (gs->entry_mode == 1) {
                    gs->state = GS_GUEST_EDIT;
                    gs->edit_cursor = gs->name_count - 1;
                } else {
                    gs->state = GS_CREDITS;
                    gs->credits_scroll = 0;
                }
            }
        }

        /* ESC = skip */
        if (inp->bits & INP_ESC) {
            if (gs->state == GS_ENTER_NAME) {
                gs->state = GS_HISCORE;
                gs->flash_timer = 30;
            } else if (gs->entry_mode == 1) {
                gs->state = GS_GUEST_EDIT;
            } else {
                gs->state = GS_CREDITS;
                gs->credits_scroll = 0;
            }
        }

        return;
    }

    if (gs->state == GS_CREDITS) {
        gs->credits_scroll++;
        /* Only allow skip after credits have scrolled a bit */
        if (gs->credits_scroll > 400 ||
            (gs->credits_scroll > 100 && inp->fire_edge)) {
            gs->state = GS_TITLE;
        }
        return;
    }

    /* --- Active gameplay --- */

    /* E = open guest editor (pauses game) */
    if (inp->last_char == 'e') {
        gs->edit_return_state = GS_PLAYING;
        gs->state = GS_GUEST_EDIT;
        gs->edit_cursor = 0;
        gs->edit_scroll = 0;
        return;
    }

    gs->party_clock++;

    /* Update timers */
    if (gs->msg_timer > 0) gs->msg_timer--;
    if (gs->flash_timer > 0) gs->flash_timer--;
    if (gs->egg_timer > 0) gs->egg_timer--;
    if (gs->rj_bubble_timer > 0) gs->rj_bubble_timer--;
    if (gs->arcade_timer > 0) gs->arcade_timer--;
    update_puffs(gs);
    update_cops(gs);

    /* Increase difficulty over time */
    gs->wave = gs->party_clock / (PARTY_FRAMES / 10);
    if (gs->wave > 9) gs->wave = 9;

    /* Player movement */
    update_player(gs, inp);

    /* Camera */
    update_camera(gs);

    /* Room-specific logic */
    switch (gs->current_room) {
        case ROOM_FOYER:     update_foyer(gs, inp); break;
        case ROOM_AMSTERDAM: update_amsterdam(gs, inp); break;
        case ROOM_TOKYO:     update_tokyo(gs, inp); break;
        case ROOM_SILICON:   update_silicon(gs, inp); break;
        case ROOM_BAYAREA:   update_bayarea(gs, inp); break;
        case ROOM_LIVING:    update_living(gs, inp); break;
    }

    /* --- Universal greeting mechanic --- */
    /* Press fire near an idle/entering guest to greet them */
    if (inp->fire_edge) {
        WORD gi;
        Player *p = &gs->player;
        for (gi = 0; gi < MAX_GUESTS; gi++) {
            Guest *g = &gs->guests[gi];
            WORD dx, dy;
            if (!g->active) continue;
            if (g->greeted) continue;
            if (g->state != GUEST_IDLE && g->state != GUEST_ENTERING
                && g->state != GUEST_WANT) continue;
            dx = p->x - g->x;
            dy = p->y - g->y;
            if (dx > -25 && dx < 25 && dy > -25 && dy < 25) {
                /* Greet this guest! */
                g->greeted = 1;
                g->bubble_timer = 80;
                {
                    static const char *greetings[] = {
                        "Happy Bday RJ!",
                        "HBD RJ!",
                        "Party time!",
                        "70 looks great!",
                        "Legend!",
                        "Cheers RJ!"
                    };
                    const char *gt = greetings[rng() % 6];
                    WORD j;
                    for (j = 0; gt[j] && j < BUBBLE_LEN - 1; j++)
                        g->bubble_text[j] = gt[j];
                    g->bubble_text[j] = 0;
                }
                /* RJ responds */
                gs->rj_bubble_timer = 60;
                {
                    static const char *responses[] = {
                        "Thanks friend!",
                        "So glad you came!",
                        "You're the best!",
                        "Party on!",
                        "Love ya!"
                    };
                    const char *rt = responses[rng() % 5];
                    WORD j;
                    for (j = 0; rt[j] && j < BUBBLE_LEN - 1; j++)
                        gs->rj_bubble[j] = rt[j];
                    gs->rj_bubble[j] = 0;
                }
                gs->score += 150;
                gs->happiness += 3;
                sfx_ding();
                break;  /* only greet one per press */
            }
        }
    }

    /* --- Arcade cabinet interaction (Silicon Valley) --- */
    if (inp->fire_edge && gs->current_room == ROOM_SILICON &&
        gs->arcade_timer <= 0 && !sinistar_is_playing()) {
        Player *p = &gs->player;
        WORD room_x = ROOM_SILICON * ROOM_W;
        /* Sinistar cabinet at room x+35, Red Baron at x+80 */
        WORD dx_sin = p->x - (room_x + 35);
        WORD dx_rb  = p->x - (room_x + 80);
        WORD near_y = (p->y > FLOOR_Y - 65 && p->y < FLOOR_Y + 5);
        if (dx_sin > -20 && dx_sin < 20 && near_y) {
            gs->arcade_timer = 100;
            gs->arcade_which = 0;
            gs->score += 200;
            sinistar_play_random();  /* pause music, play RJ's voice! */
            gs->rj_bubble_timer = 80;
            {
                static const char *sin_quotes[] = {
                    "BEWARE I LIVE!",
                    "I HUNGER!",
                    "RUN RUN RUN!",
                    "BEWARE COWARD!"
                };
                const char *s = sin_quotes[rng() % 4];
                WORD j;
                for (j = 0; s[j] && j < BUBBLE_LEN - 1; j++)
                    gs->rj_bubble[j] = s[j];
                gs->rj_bubble[j] = 0;
            }
        }
        else if (dx_rb > -20 && dx_rb < 20 && near_y) {
            gs->arcade_timer = 80;
            gs->arcade_which = 1;
            gs->score += 200;
            sfx_cheer();
            gs->rj_bubble_timer = 60;
            {
                const char *s = "Tally ho!";
                WORD j;
                for (j = 0; s[j] && j < BUBBLE_LEN - 1; j++)
                    gs->rj_bubble[j] = s[j];
                gs->rj_bubble[j] = 0;
            }
        }
    }

    /* Update guest bubble timers */
    {
        WORD gi;
        for (gi = 0; gi < MAX_GUESTS; gi++) {
            Guest *g = &gs->guests[gi];
            if (!g->active) continue;
            if (g->bubble_timer > 0) g->bubble_timer--;
        }
    }

    /* Easter egg: crunch neck when going between rooms */
    if (gs->current_room != gs->player.x / ROOM_W) {
        /* Room transition */
    }
    /* Crunch neck easter egg: if walking fast between rooms */
    {
        static WORD last_room = -1;
        if (gs->current_room != last_room) {
            if (last_room >= 0 && gs->egg_timer <= 0) {
                if ((rng() % 5) == 0) {
                    WORD j;
                    for (j = 0; EGG_CRUNCH[j] && j < 39; j++)
                        gs->egg_text[j] = EGG_CRUNCH[j];
                    gs->egg_text[j] = 0;
                    gs->egg_timer = 80;
                }
            }
            last_room = gs->current_room;
        }
    }

    /* Joe Pillow easter egg in Bay Area */
    if (gs->current_room == ROOM_BAYAREA && gs->egg_timer <= 0) {
        if ((gs->party_clock % 500) == 250) {
            WORD j;
            for (j = 0; EGG_JOEPILLOW[j] && j < 39; j++)
                gs->egg_text[j] = EGG_JOEPILLOW[j];
            gs->egg_text[j] = 0;
            gs->egg_timer = 120;
        }
    }

    /* Check game over conditions */
    if (gs->lives <= 0) {
        gs->state = GS_GAMEOVER;
        gs->flash_timer = 30; /* cooldown before accepting input */
        sfx_buzzer();
    }

    /* Check party end */
    if (gs->party_clock >= PARTY_FRAMES) {
        if (gs->state == GS_PLAYING) {
            if (!gs->finale) {
                gs->state = GS_WIN;
                gs->score += 500;
                sfx_cheer();
            }
        }
    }

    /* Happiness affects score */
    if ((gs->party_clock % 50) == 0) {
        if (gs->happiness > 50)
            gs->score += gs->happiness / 10;
    }

    /* Clamp happiness */
    if (gs->happiness < 0) gs->happiness = 0;
    if (gs->happiness > 100) gs->happiness = 100;
}
