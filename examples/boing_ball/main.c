/*
 * Boing Ball - Reimagined
 *
 * A modern take on the iconic Amiga Boing Ball demo.
 * Features: 3D-lit checkered sphere, copper gradient sky,
 * perspective grid floor, shadow, bounce physics with
 * squash/stretch, and synthesized metallic clank sound.
 *
 * Keys: ESC=quit, SPACE=credits, C=rainbow copper mode
 * Screen is draggable - pull down to see Workbench.
 *
 * Target: A1200 (68020), 320x256, 5 bitplanes (32 colors)
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <graphics/rastport.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

#include <string.h>

#include "tables.h"
#include "physics.h"
#include "sphere.h"
#include "copper.h"
#include "render.h"
#include "sound.h"

#include <bridge_client.h>

/* Raw key codes */
#define KEY_ESC   0x45
#define KEY_SPACE 0x40
#define KEY_C     0x33
#define KEY_S     0x21
#define KEY_F     0x23

/* Libraries */
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;

/* Screen & window */
static struct Screen *screen = NULL;
static struct Window *win = NULL;

/* Double buffering */
static struct ScreenBuffer *sbuf[2] = { NULL, NULL };
static struct MsgPort *disp_port = NULL;
static struct MsgPort *safe_port = NULL;
static BOOL safe_to_write = TRUE;
static BOOL disp_pending = FALSE;
static WORD cur_buf = 0;

/* Palette: 32 colors for 5 bitplanes */
static UWORD palette[32] = {
    0x004,                                          /* 0: sky base (copper overrides) */
    0xF22, 0xD11, 0xB00, 0x900, 0x700, 0x500, 0x300,  /* 1-7: red shades */
    0xFFF, 0xDDD, 0xBBB, 0x999, 0x777, 0x555, 0x333,  /* 8-14: white shades */
    0xFFF,                                          /* 15: specular */
    0x112,                                          /* 16: shadow */
    0x863,                                          /* 17: floor base */
    0x542,                                          /* 18: floor grid */
    0xA85,                                          /* 19: floor accent */
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000,      /* 20-25: spare */
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000        /* 26-31: spare */
};

/* State */
static BOOL show_credits = FALSE;
static BOOL rainbow_mode = FALSE;

/* Bridge variables */
static long frame_count = 0;
static long ball_px = 0, ball_py = 0;

static BOOL open_libs(void)
{
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    if (!IntuitionBase) return FALSE;
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
    if (!GfxBase) return FALSE;
    return TRUE;
}

static void close_libs(void)
{
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
}

static BOOL open_display(void)
{
    WORD i;
    struct TagItem scrTags[] = {
        { SA_Width, SCREEN_W },
        { SA_Height, SCREEN_H },
        { SA_Depth, SPHERE_DEPTH },
        { SA_DisplayID, LORES_KEY },
        { SA_ShowTitle, FALSE },
        { SA_Quiet, TRUE },
        { SA_Type, CUSTOMSCREEN },
        { TAG_DONE, 0 }
    };

    screen = OpenScreenTagList(NULL, scrTags);
    if (!screen) return FALSE;

    /* Set palette */
    for (i = 0; i < 32; i++) {
        SetRGB4(&screen->ViewPort, i,
            (palette[i] >> 8) & 0xF,
            (palette[i] >> 4) & 0xF,
            palette[i] & 0xF);
    }

    /* Open backdrop window for IDCMP key events */
    win = OpenWindowTags(NULL,
        WA_CustomScreen, (ULONG)screen,
        WA_Left, 0,
        WA_Top, 0,
        WA_Width, SCREEN_W,
        WA_Height, SCREEN_H,
        WA_Backdrop, TRUE,
        WA_Borderless, TRUE,
        WA_Activate, TRUE,
        WA_RMBTrap, TRUE,
        WA_IDCMP, IDCMP_RAWKEY,
        TAG_DONE);
    if (!win) return FALSE;

    /* Double buffering via ScreenBuffer */
    disp_port = CreateMsgPort();
    safe_port = CreateMsgPort();
    if (!disp_port || !safe_port) return FALSE;

    sbuf[0] = AllocScreenBuffer(screen, NULL, SB_SCREEN_BITMAP);
    sbuf[1] = AllocScreenBuffer(screen, NULL, SB_COPY_BITMAP);
    if (!sbuf[0] || !sbuf[1]) return FALSE;

    sbuf[0]->sb_DBufInfo->dbi_DispMessage.mn_ReplyPort = disp_port;
    sbuf[1]->sb_DBufInfo->dbi_DispMessage.mn_ReplyPort = disp_port;
    sbuf[0]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = safe_port;
    sbuf[1]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = safe_port;

    return TRUE;
}

static void swap_buffers(void)
{
    if (!safe_to_write) {
        while (!GetMsg(safe_port))
            WaitPort(safe_port);
        safe_to_write = TRUE;
    }

    if (disp_pending) {
        while (!GetMsg(disp_port))
            WaitPort(disp_port);
        disp_pending = FALSE;
    }

    ChangeScreenBuffer(screen, sbuf[cur_buf]);
    disp_pending = TRUE;
    safe_to_write = FALSE;

    cur_buf ^= 1;
    screen->RastPort.BitMap = sbuf[cur_buf]->sb_BitMap;
}

static void close_display(void)
{
    if (!safe_to_write) {
        while (!GetMsg(safe_port))
            WaitPort(safe_port);
    }
    if (disp_pending) {
        while (!GetMsg(disp_port))
            WaitPort(disp_port);
    }

    if (sbuf[1]) FreeScreenBuffer(screen, sbuf[1]);
    if (sbuf[0]) FreeScreenBuffer(screen, sbuf[0]);
    if (safe_port) DeleteMsgPort(safe_port);
    if (disp_port) DeleteMsgPort(disp_port);
    if (win) CloseWindow(win);
    if (screen) CloseScreen(screen);
}

/* Process IDCMP messages, return FALSE to quit */
static BOOL handle_input(BallState *ball)
{
    struct IntuiMessage *imsg;

    while ((imsg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
        ULONG cls = imsg->Class;
        UWORD code = imsg->Code;
        ReplyMsg((struct Message *)imsg);

        if (cls == IDCMP_RAWKEY && !(code & IECODE_UP_PREFIX)) {
            switch (code) {
                case KEY_ESC:
                    return FALSE;
                case KEY_SPACE:
                    show_credits = !show_credits;
                    break;
                case KEY_C:
                    rainbow_mode = !rainbow_mode;
                    copper_set_rainbow(&screen->ViewPort, rainbow_mode);
                    break;
                case KEY_F:
                    /* Faster: scale velocities up 25% */
                    ball->vx = (ball->vx * 5) / 4;
                    ball->vy = (ball->vy * 5) / 4;
                    if (ball->rot_speed < 24) ball->rot_speed += 2;
                    break;
                case KEY_S:
                    /* Slower: scale velocities down 20% */
                    ball->vx = (ball->vx * 4) / 5;
                    ball->vy = (ball->vy * 4) / 5;
                    if (ball->rot_speed > 2) ball->rot_speed -= 2;
                    break;
            }
        }
    }
    return TRUE;
}

/* Get a layer-free RastPort pointing at the current back buffer.
 * This bypasses all Intuition layer clipping so we can draw
 * to the full screen including the title bar area. */
static struct RastPort draw_rp;

static struct RastPort *get_draw_rp(void)
{
    InitRastPort(&draw_rp);
    draw_rp.BitMap = sbuf[cur_buf]->sb_BitMap;
    return &draw_rp;
}

int main(int argc, char **argv)
{
    BallState ball;
    ULONG signals;
    struct RastPort *rp;

    if (!open_libs()) {
        close_libs();
        return 20;
    }

    tables_init();

    if (!open_display()) {
        close_display();
        close_libs();
        return 20;
    }

    /* Init copper sky gradient */
    copper_init(&screen->ViewPort);

    /* Pre-render sphere frames */
    sphere_init();

    /* Init sound */
    sound_init();

    /* Init rendering */
    render_init();

    /* Init physics */
    physics_init(&ball);

    /* Init bridge */
    ab_init("boing_ball");
    ab_register_var("frame", AB_TYPE_I32, &frame_count);
    ab_register_var("ball_x", AB_TYPE_I32, &ball_px);
    ab_register_var("ball_y", AB_TYPE_I32, &ball_py);
    ab_log(AB_INFO, "Boing Ball Reimagined started");

    /* Draw initial frame on both buffers */
    rp = get_draw_rp();
    render_frame(rp, &ball, show_credits);
    swap_buffers();
    rp = get_draw_rp();
    render_frame(rp, &ball, show_credits);

    for (;;) {
        /* Check CTRL-C */
        signals = SetSignal(0L, 0L);
        if (signals & SIGBREAKF_CTRL_C)
            break;

        /* Handle keyboard input */
        if (!handle_input(&ball))
            break;

        /* Update physics */
        physics_update(&ball);

        /* Play clank on bounce */
        if (ball.bounced) {
            sound_play_boing();
        }

        /* Update bridge variables */
        ball_px = (long)FP_TO_INT(ball.x);
        ball_py = (long)FP_TO_INT(ball.y);
        frame_count++;

        /* Render full frame to unlayered back buffer */
        rp = get_draw_rp();
        render_frame(rp, &ball, show_credits);

        /* Swap display buffers */
        swap_buffers();

        /* Poll bridge */
        ab_poll();

        /* Sync to vertical blank */
        WaitTOF();
    }

    ab_log(AB_INFO, "Boing Ball shutting down");
    ab_cleanup();

    sound_cleanup();
    sphere_cleanup();
    copper_cleanup();
    close_display();
    close_libs();

    return 0;
}
