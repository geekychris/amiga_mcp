/*
 * Frank the Frog - Graphics layer
 * Custom screen with double buffering via ScreenBuffer.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

#include "game.h"
#include "gfx.h"

extern struct IntuitionBase *IntuitionBase;
extern struct GfxBase *GfxBase;

static struct Screen *screen = NULL;
static struct ScreenBuffer *sb[2] = {NULL, NULL};
static struct RastPort rp_back;
static struct MsgPort *dbufport = NULL;
static int current_buf = 0;

/* Frogger palette: 16 colors (4-bit RGB each component) */
static UWORD palette[16] = {
    0x000,  /* 0  BG: black */
    0x070,  /* 1  GRASS: green */
    0x333,  /* 2  ROAD: dark gray */
    0x00B,  /* 3  WATER: blue */
    0x0E0,  /* 4  FROG: bright green */
    0x060,  /* 5  FROG_DARK: dark green */
    0x830,  /* 6  LOG: brown */
    0xD00,  /* 7  CAR_RED: red */
    0xED0,  /* 8  CAR_YELLOW: yellow */
    0x08D,  /* 9  CAR_BLUE: blue */
    0x606,  /* 10 TRUCK: purple */
    0x050,  /* 11 TURTLE: dark green */
    0x8E4,  /* 12 LILY: light green */
    0xFFF,  /* 13 WHITE */
    0x003,  /* 14 HUD_BG: dark blue */
    0xCC0,  /* 15 ROAD_LINE: yellow dashes */
};

void gfx_set_palette(void)
{
    int i;
    struct ViewPort *vp;

    if (!screen) return;
    vp = &screen->ViewPort;

    for (i = 0; i < 16; i++) {
        SetRGB4(vp, (long)i,
                (long)((palette[i] >> 8) & 0xF),
                (long)((palette[i] >> 4) & 0xF),
                (long)(palette[i] & 0xF));
    }
}

int gfx_init(void)
{
    screen = OpenScreenTags(NULL,
        SA_Width, (ULONG)SCREEN_W,
        SA_Height, (ULONG)SCREEN_H,
        SA_Depth, (ULONG)SCREEN_DEPTH,
        SA_DisplayID, (ULONG)0x00000000,  /* LORES PAL */
        SA_Title, (ULONG)"Frank the Frog",
        SA_ShowTitle, (ULONG)FALSE,
        SA_Quiet, (ULONG)TRUE,
        SA_Type, (ULONG)CUSTOMSCREEN,
        TAG_DONE);

    if (!screen) return 1;

    /* Set up double buffering */
    sb[0] = AllocScreenBuffer(screen, NULL, SB_SCREEN_BITMAP);
    sb[1] = AllocScreenBuffer(screen, NULL, SB_COPY_BITMAP);

    if (!sb[0] || !sb[1]) {
        gfx_cleanup();
        return 1;
    }

    dbufport = CreateMsgPort();
    if (!dbufport) {
        gfx_cleanup();
        return 1;
    }

    sb[0]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = dbufport;
    sb[1]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = dbufport;

    /* Init back buffer rastport */
    InitRastPort(&rp_back);
    rp_back.BitMap = sb[1]->sb_BitMap;
    current_buf = 0;

    gfx_set_palette();

    return 0;
}

void gfx_cleanup(void)
{
    if (sb[1]) { FreeScreenBuffer(screen, sb[1]); sb[1] = NULL; }
    if (sb[0]) { FreeScreenBuffer(screen, sb[0]); sb[0] = NULL; }
    if (dbufport) { DeleteMsgPort(dbufport); dbufport = NULL; }
    if (screen) { CloseScreen(screen); screen = NULL; }
}

struct RastPort *gfx_backbuffer(void)
{
    return &rp_back;
}

void gfx_swap(void)
{
    /* Display the back buffer */
    current_buf ^= 1;
    ChangeScreenBuffer(screen, sb[current_buf]);

    /* Wait for safe to draw */
    WaitPort(dbufport);
    while (GetMsg(dbufport)) ;

    /* Point rastport at the new back buffer */
    rp_back.BitMap = sb[current_buf ^ 1]->sb_BitMap;
}

void gfx_vsync(void)
{
    WaitTOF();
}

struct Screen *gfx_screen(void)
{
    return screen;
}
