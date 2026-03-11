/*
 * Frank the Frog - A Frogger clone for Amiga
 * Uses AmigaBridge for remote monitoring.
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <hardware/cia.h>
#include <hardware/custom.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#include "bridge_client.h"

#include "game.h"
#include "gfx.h"
#include "playfield.h"
#include "frank.h"
#include "lanes.h"
#include "score.h"
#include "sound.h"
#include "modplay.h"

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;

/* Joystick reading via JOY1DAT hardware register */
extern struct Custom custom;

static UWORD prev_joy = 0;

static void read_joystick(int *dx, int *dy, int *fire)
{
    volatile UWORD joy = custom.joy1dat;
    UBYTE ciaa_pra;

    *dx = 0;
    *dy = 0;

    /* Decode joystick directions from JOY1DAT */
    int right = (joy >> 1) & 1;
    int left  = (joy >> 9) & 1;
    int down  = ((joy >> 1) ^ joy) & 1;
    int up    = ((joy >> 9) ^ (joy >> 8)) & 1;

    /* Edge detection: only register new presses */
    int prev_right = (prev_joy >> 1) & 1;
    int prev_left  = (prev_joy >> 9) & 1;
    int prev_down  = ((prev_joy >> 1) ^ prev_joy) & 1;
    int prev_up    = ((prev_joy >> 9) ^ (prev_joy >> 8)) & 1;

    if (right && !prev_right) *dx = 1;
    if (left && !prev_left)   *dx = -1;
    if (down && !prev_down)   *dy = 1;
    if (up && !prev_up)       *dy = -1;

    prev_joy = joy;

    /* Fire button: bit 7 of CIA-A PRA, active low */
    ciaa_pra = *((volatile UBYTE *)0xBFE001);
    *fire = (ciaa_pra & 0x80) ? 0 : 1;
}

/* Game state */
static int game_state = STATE_TITLE;
static int transition_frames = 0; /* frames to wait during transitions */
static ULONG frame_count = 0;
static LONG bridge_ok = 0;

/* Bridge variables for remote monitoring */
static LONG var_score = 0;
static LONG var_lives = 3;
static LONG var_level = 1;
static LONG var_frog_col = 0;
static LONG var_frog_row = 0;
static LONG var_state = 0;

/* Hook: get game status */
static int hook_status(const char *args, char *resultBuf, int bufSize)
{
    sprintf(resultBuf, "state=%ld score=%ld lives=%ld level=%ld frog=(%ld,%ld) frame=%lu",
            (long)game_state, (long)var_score, (long)var_lives,
            (long)var_level, (long)var_frog_col, (long)var_frog_row,
            (unsigned long)frame_count);
    resultBuf[bufSize - 1] = '\0';
    return 0;
}

/* Hook: reset game */
static int hook_reset(const char *args, char *resultBuf, int bufSize)
{
    game_state = STATE_TITLE;
    transition_frames = 0;
    strncpy(resultBuf, "Game reset to title", bufSize - 1);
    resultBuf[bufSize - 1] = '\0';
    return 0;
}

static void update_bridge_vars(void)
{
    var_score = (LONG)score_get();
    var_lives = (LONG)score_lives();
    var_level = (LONG)score_level();
    var_frog_col = (LONG)frank_col();
    var_frog_row = (LONG)frank_row();
    var_state = (LONG)game_state;
}

static void draw_title(struct RastPort *rp)
{
    SetAPen(rp, (long)COL_BG);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);

    /* Big frog face */
    SetAPen(rp, (long)COL_FROG);
    RectFill(rp, 130, 40, 190, 90);
    /* Eyes */
    SetAPen(rp, (long)COL_WHITE);
    RectFill(rp, 135, 42, 148, 55);
    RectFill(rp, 170, 42, 183, 55);
    /* Pupils */
    SetAPen(rp, (long)COL_BG);
    RectFill(rp, 140, 46, 144, 51);
    RectFill(rp, 175, 46, 179, 51);
    /* Mouth */
    SetAPen(rp, (long)COL_FROG_DARK);
    Move(rp, 145, 75);
    Draw(rp, 155, 80);
    Draw(rp, 165, 75);
    /* Nostrils */
    WritePixel(rp, 150, 65);
    WritePixel(rp, 168, 65);

    /* Title text */
    SetAPen(rp, (long)COL_FROG);
    SetBPen(rp, (long)COL_BG);
    Move(rp, (SCREEN_W - 14 * 8) / 2, 110);
    Text(rp, (CONST_STRPTR)"FRANK THE FROG", 14L);

    SetAPen(rp, (long)COL_WHITE);
    Move(rp, (SCREEN_W - 19 * 8) / 2, 150);
    Text(rp, (CONST_STRPTR)"Press FIRE to start", 19L);

    /* Controls help */
    SetAPen(rp, (long)COL_ROAD_LINE);
    Move(rp, (SCREEN_W - 24 * 8) / 2, 180);
    Text(rp, (CONST_STRPTR)"Arrows/Joy2 to move ", 20L);
    Move(rp, (SCREEN_W - 24 * 8) / 2, 195);
    Text(rp, (CONST_STRPTR)"Space/Fire  to start", 20L);
    Move(rp, (SCREEN_W - 24 * 8) / 2, 210);
    Text(rp, (CONST_STRPTR)"ESC to quit         ", 20L);
}

static void draw_gameover(struct RastPort *rp)
{
    char buf[32];

    /* Semi-transparent overlay effect */
    SetAPen(rp, (long)COL_BG);
    RectFill(rp, 60, 80, 260, 170);
    SetAPen(rp, (long)COL_WHITE);
    Move(rp, 60, 80); Draw(rp, 260, 80);
    Draw(rp, 260, 170); Draw(rp, 60, 170); Draw(rp, 60, 80);

    SetAPen(rp, (long)COL_CAR_RED);
    SetBPen(rp, (long)COL_BG);
    Move(rp, (SCREEN_W - 9 * 8) / 2, 105);
    Text(rp, (CONST_STRPTR)"GAME OVER", 9L);

    sprintf(buf, "SCORE: %06ld", (long)score_get());
    SetAPen(rp, (long)COL_WHITE);
    Move(rp, (SCREEN_W - (int)strlen(buf) * 8) / 2, 130);
    Text(rp, (CONST_STRPTR)buf, (long)strlen(buf));

    SetAPen(rp, (long)COL_ROAD_LINE);
    Move(rp, (SCREEN_W - 20 * 8) / 2, 155);
    Text(rp, (CONST_STRPTR)"Press FIRE to retry ", 20L);
}

static void draw_level_complete(struct RastPort *rp)
{
    char buf[32];

    SetAPen(rp, (long)COL_BG);
    RectFill(rp, 80, 90, 240, 150);
    SetAPen(rp, (long)COL_FROG);
    Move(rp, 80, 90); Draw(rp, 240, 90);
    Draw(rp, 240, 150); Draw(rp, 80, 150); Draw(rp, 80, 90);

    SetAPen(rp, (long)COL_FROG);
    SetBPen(rp, (long)COL_BG);
    Move(rp, (SCREEN_W - 14 * 8) / 2, 115);
    Text(rp, (CONST_STRPTR)"LEVEL COMPLETE", 14L);

    sprintf(buf, "+1000 BONUS!");
    SetAPen(rp, (long)COL_CAR_YELLOW);
    Move(rp, (SCREEN_W - 12 * 8) / 2, 140);
    Text(rp, (CONST_STRPTR)buf, (long)strlen(buf));
}

/* Draw a full frame for both buffers (used during state transitions) */
static void draw_both_buffers(void (*draw_fn)(struct RastPort *))
{
    struct RastPort *rp;

    rp = gfx_backbuffer();
    draw_fn(rp);
    gfx_swap();
    gfx_vsync();

    rp = gfx_backbuffer();
    draw_fn(rp);
    gfx_swap();
    gfx_vsync();
}

int main(void)
{
    struct Window *win = NULL;
    struct RastPort *rp;
    int running = 1;
    int joy_dx, joy_dy, joy_fire;
    int prev_fire = 0;
    int death_done;
    int ride_dx;

    /* Open libraries */
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 36);
    if (!IntuitionBase) return 1;

    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 36);
    if (!GfxBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    /* Connect to bridge */
    if (ab_init("frank_frog") == 0) {
        bridge_ok = 1;
        AB_I("Frank the Frog starting");

        ab_register_var("score", AB_TYPE_I32, &var_score);
        ab_register_var("lives", AB_TYPE_I32, &var_lives);
        ab_register_var("level", AB_TYPE_I32, &var_level);
        ab_register_var("frog_col", AB_TYPE_I32, &var_frog_col);
        ab_register_var("frog_row", AB_TYPE_I32, &var_frog_row);
        ab_register_var("game_state", AB_TYPE_I32, &var_state);
        ab_register_hook("status", "Get game status", hook_status);
        ab_register_hook("reset", "Reset to title screen", hook_reset);
    }

    /* Init graphics */
    if (gfx_init() != 0) {
        AB_E("Failed to open screen");
        if (bridge_ok) ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    /* Init sound + music */
    if (sound_init() != 0) {
        AB_W("Sound init failed - continuing without sound");
    }
    modplay_start();

    /* Open a tiny invisible IDCMP window on our screen for keyboard */
    win = OpenWindowTags(NULL,
        WA_CustomScreen, (ULONG)gfx_screen(),
        WA_Left, 0, WA_Top, 0,
        WA_Width, SCREEN_W, WA_Height, SCREEN_H,
        WA_Borderless, (ULONG)TRUE,
        WA_Backdrop, (ULONG)TRUE,
        WA_RMBTrap, (ULONG)TRUE,
        WA_IDCMP, IDCMP_RAWKEY,
        WA_Activate, (ULONG)TRUE,
        TAG_DONE);

    /* Init game systems */
    score_init();
    frank_init();
    lanes_init(1);

    AB_I("Game initialized, entering main loop");

    /* Draw title on both buffers for clean display */
    game_state = STATE_TITLE;
    draw_both_buffers(draw_title);

    while (running) {
        /* Read joystick first */
        read_joystick(&joy_dx, &joy_dy, &joy_fire);

        /* Then keyboard overrides/merges via IDCMP */
        if (win) {
            struct IntuiMessage *imsg;
            while ((imsg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
                ULONG cl = imsg->Class;
                UWORD code = imsg->Code;
                ReplyMsg((struct Message *)imsg);
                if (cl == IDCMP_RAWKEY && !(code & 0x80)) {
                    /* Key down only (bit 7 clear) */
                    if (code == 0x45) {
                        running = 0;
                    } else if (code == 0x40 || code == 0x44) {
                        /* SPACE or RETURN = fire */
                        joy_fire = 1;
                    } else if (code == 0x4C) {
                        joy_dy = -1;
                    } else if (code == 0x4D) {
                        joy_dy = 1;
                    } else if (code == 0x4F) {
                        joy_dx = -1;
                    } else if (code == 0x4E) {
                        joy_dx = 1;
                    }
                }
            }
        }

        if (!running) break;

        rp = gfx_backbuffer();

        switch (game_state) {
        case STATE_TITLE:
            /* Title is already drawn on both buffers.
             * Just wait for fire to start game. */
            if (joy_fire && !prev_fire) {
                game_state = STATE_PLAYING;
                score_init();
                frank_init();
                lanes_init(1);
                lanes_reset_home();
                AB_I("Game started");
            }
            break;

        case STATE_PLAYING:
            /* Move frog */
            if (joy_dx || joy_dy) {
                int old_highest = frank_highest_row();
                frank_move(joy_dx, joy_dy);
                sound_hop();

                /* Score for reaching a new highest row */
                if (frank_highest_row() < old_highest) {
                    score_add(10);
                }
            }

            /* Update lanes */
            lanes_tick();

            /* River riding */
            if (frank_row() >= ROW_RIVER_5 && frank_row() <= ROW_RIVER_1) {
                if (!lanes_check_river(frank_x(), frank_y(), &ride_dx)) {
                    AB_I("Splash! row=%ld col=%ld",
                         (long)frank_row(), (long)frank_col());
                    frank_start_death();
                    game_state = STATE_DYING;
                    sound_splash();
                } else if (ride_dx != 0) {
                    int new_x = frank_x() + ride_dx;
                    if (new_x < -4 || new_x + TILE_W > SCREEN_W + 4) {
                        AB_I("Carried off screen!");
                        frank_start_death();
                        game_state = STATE_DYING;
                        sound_splash();
                    } else {
                        frank_set_x(new_x);
                    }
                }
            }

            /* Car collision */
            if (game_state == STATE_PLAYING &&
                frank_row() >= ROW_ROAD_5 && frank_row() <= ROW_ROAD_1) {
                if (lanes_check_car(frank_x(), frank_y())) {
                    AB_I("Splat! row=%ld col=%ld",
                         (long)frank_row(), (long)frank_col());
                    frank_start_death();
                    game_state = STATE_DYING;
                    sound_splat();
                }
            }

            /* Home base check */
            if (game_state == STATE_PLAYING && frank_row() == ROW_HOME) {
                int slot = lanes_check_home(frank_x());
                if (slot >= 0) {
                    lanes_fill_home(slot);
                    score_add(50);
                    sound_home();
                    AB_I("Home slot %ld filled!", (long)slot);

                    if (lanes_all_home()) {
                        game_state = STATE_LEVEL;
                        transition_frames = 60; /* 1.2 sec pause */
                        sound_levelup();
                        AB_I("Level %ld complete!", (long)score_level());
                    } else {
                        frank_init();
                    }
                } else {
                    frank_start_death();
                    game_state = STATE_DYING;
                    sound_splash();
                }
            }

            /* Redraw everything */
            playfield_draw(rp);
            lanes_draw(rp);
            frank_draw(rp);
            score_draw(rp);
            break;

        case STATE_DYING:
            death_done = !frank_die_tick();

            /* Keep lanes moving during death */
            lanes_tick();

            playfield_draw(rp);
            lanes_draw(rp);
            frank_draw(rp);
            score_draw(rp);

            if (death_done) {
                score_lose_life();
                if (score_lives() <= 0) {
                    game_state = STATE_GAMEOVER;
                    transition_frames = 30; /* brief pause before showing game over */
                    sound_gameover();
                    AB_I("Game over! Score: %ld", (long)score_get());
                } else {
                    frank_init();
                    game_state = STATE_PLAYING;
                }
            }
            break;

        case STATE_LEVEL:
            /* Show level complete, keep drawing the scene */
            playfield_draw(rp);
            lanes_draw(rp);
            score_draw(rp);
            draw_level_complete(rp);

            transition_frames--;
            if (transition_frames <= 0) {
                score_next_level();
                lanes_reset_home();
                lanes_init(score_level());
                frank_init();
                game_state = STATE_PLAYING;
                AB_I("Starting level %ld", (long)score_level());
            }
            break;

        case STATE_GAMEOVER:
            /* Keep lanes animating behind game over */
            lanes_tick();
            playfield_draw(rp);
            lanes_draw(rp);
            score_draw(rp);
            draw_gameover(rp);

            if (transition_frames > 0) {
                transition_frames--;
            } else if (joy_fire && !prev_fire) {
                /* Return to title: draw on both buffers */
                game_state = STATE_TITLE;
                draw_both_buffers(draw_title);
                prev_fire = joy_fire;
                /* Skip the normal swap below since we already swapped */
                goto next_frame;
            }
            break;
        }

        prev_fire = joy_fire;

        gfx_swap();
        gfx_vsync();
        modplay_tick();

next_frame:
        frame_count++;

        /* Update bridge every 30 frames */
        if (bridge_ok && (frame_count % 30) == 0) {
            update_bridge_vars();
            ab_push_var("score");
            ab_push_var("lives");
            ab_push_var("level");
            ab_push_var("frog_col");
            ab_push_var("frog_row");
            ab_push_var("game_state");
            ab_heartbeat();
        }

        /* Poll bridge for incoming messages */
        if (bridge_ok) {
            ab_poll();
            /* Check if bridge told us to shut down */
            if (!ab_is_connected()) {
                bridge_ok = 0;
            }
        }
    }

    AB_I("Frank the Frog exiting");

    modplay_stop();
    sound_cleanup();
    if (win) CloseWindow(win);
    gfx_cleanup();
    if (bridge_ok) ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    return 0;
}
