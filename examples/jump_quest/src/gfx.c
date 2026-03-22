/*
 * Jump Quest - Graphics System
 * Screen setup, double buffering, tile drawing
 */
#include "game.h"
#include <string.h>

static struct ScreenBuffer *sb[2] = {NULL, NULL};
static struct RastPort rp_back;
static struct MsgPort *dbufport = NULL;
static int current_buf = 0;

/* 16-color palette: R, G, B (4-bit each) */
static UWORD palette[16] = {
    0x59C,  /* 0: sky blue */
    0x47A,  /* 1: darker sky */
    0xA62,  /* 2: brown */
    0x741,  /* 3: dark brown */
    0x4A2,  /* 4: green */
    0x381,  /* 5: dark green */
    0xD83,  /* 6: orange/brick */
    0xA52,  /* 7: dark orange */
    0xEC9,  /* 8: skin */
    0x999,  /* 9: grey */
    0x35A,  /* 10: blue/denim */
    0xEEE,  /* 11: white */
    0x111,  /* 12: black */
    0xDD3,  /* 13: yellow */
    0xD22,  /* 14: red */
    0x4D4,  /* 15: light green */
};

struct Screen *gameScreen = NULL;

int gfx_init(void) {
    int i;

    gameScreen = OpenScreenTags(NULL,
        SA_Width, (ULONG)SCREEN_W,
        SA_Height, (ULONG)SCREEN_H,
        SA_Depth, (ULONG)SCREEN_DEPTH,
        SA_DisplayID, (ULONG)0x00000000,
        SA_Title, (ULONG)"Jump Quest",
        SA_ShowTitle, (ULONG)FALSE,
        SA_Quiet, (ULONG)TRUE,
        SA_Type, (ULONG)CUSTOMSCREEN,
        TAG_DONE);

    if (!gameScreen) return 0;

    /* Set palette */
    for (i = 0; i < 16; i++) {
        SetRGB4(&gameScreen->ViewPort,
                (long)i,
                (long)((palette[i] >> 8) & 0xF),
                (long)((palette[i] >> 4) & 0xF),
                (long)(palette[i] & 0xF));
    }

    /* Double buffering */
    sb[0] = AllocScreenBuffer(gameScreen, NULL, SB_SCREEN_BITMAP);
    sb[1] = AllocScreenBuffer(gameScreen, NULL, SB_COPY_BITMAP);

    if (!sb[0] || !sb[1]) {
        gfx_cleanup();
        return 0;
    }

    dbufport = CreateMsgPort();
    if (!dbufport) {
        gfx_cleanup();
        return 0;
    }

    sb[0]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = dbufport;
    sb[1]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = dbufport;

    /* Init back buffer rastport */
    InitRastPort(&rp_back);
    rp_back.BitMap = sb[1]->sb_BitMap;
    current_buf = 0;

    return 1;
}

void gfx_cleanup(void) {
    if (sb[1]) { FreeScreenBuffer(gameScreen, sb[1]); sb[1] = NULL; }
    if (sb[0]) { FreeScreenBuffer(gameScreen, sb[0]); sb[0] = NULL; }
    if (dbufport) { DeleteMsgPort(dbufport); dbufport = NULL; }
    if (gameScreen) { CloseScreen(gameScreen); gameScreen = NULL; }
}

void gfx_swap(void) {
    /* Display the back buffer */
    current_buf ^= 1;
    ChangeScreenBuffer(gameScreen, sb[current_buf]);

    /* Wait for safe to draw */
    WaitPort(dbufport);
    while (GetMsg(dbufport)) ;

    /* Point rastport at the new back buffer */
    rp_back.BitMap = sb[current_buf ^ 1]->sb_BitMap;
}

struct RastPort *gfx_backbuffer(void) {
    return &rp_back;
}

/*
 * Draw a tile at screen position (sx, sy)
 * All tiles drawn with graphics primitives for simplicity
 */
void gfx_draw_tile(struct RastPort *rp, int tile_id, int sx, int sy) {
    int ex = sx + TILE_SIZE - 1;
    int ey = sy + TILE_SIZE - 1;

    /* Skip tiles that are partially off-screen to avoid draw artifacts */
    if (sx < 0 || ex >= SCREEN_W || sy < 0 || ey >= HUD_Y) return;

    switch (tile_id) {
    case TILE_EMPTY:
        SetAPen(rp, COL_SKY);
        RectFill(rp, sx, sy, ex, ey);
        break;

    case TILE_GROUND:
        SetAPen(rp, COL_BROWN);
        RectFill(rp, sx, sy, ex, ey);
        SetAPen(rp, COL_DKBROWN);
        Move(rp, sx, sy + 4); Draw(rp, ex, sy + 4);
        Move(rp, sx + 4, sy + 8); Draw(rp, ex, sy + 8);
        Move(rp, sx, sy + 12); Draw(rp, sx + 8, sy + 12);
        break;

    case TILE_GRASS:
        SetAPen(rp, COL_BROWN);
        RectFill(rp, sx, sy + 4, ex, ey);
        SetAPen(rp, COL_GREEN);
        RectFill(rp, sx, sy, ex, sy + 3);
        SetAPen(rp, COL_DKGREEN);
        RectFill(rp, sx + 2, sy, sx + 3, sy + 1);
        RectFill(rp, sx + 8, sy, sx + 9, sy + 1);
        RectFill(rp, sx + 13, sy, sx + 14, sy + 1);
        break;

    case TILE_BRICK:
        SetAPen(rp, COL_ORANGE);
        RectFill(rp, sx, sy, ex, ey);
        SetAPen(rp, COL_DKORANGE);
        Move(rp, sx, sy + 7); Draw(rp, ex, sy + 7);
        Move(rp, sx + 8, sy); Draw(rp, sx + 8, sy + 6);
        Move(rp, sx, sy + 15); Draw(rp, ex, sy + 15);
        Move(rp, sx + 4, sy + 8); Draw(rp, sx + 4, sy + 14);
        Move(rp, sx + 12, sy + 8); Draw(rp, sx + 12, sy + 14);
        break;

    case TILE_QBLOCK:
        SetAPen(rp, COL_YELLOW);
        RectFill(rp, sx, sy, ex, ey);
        SetAPen(rp, COL_DKORANGE);
        Move(rp, sx, sy); Draw(rp, ex, sy);
        Move(rp, sx, ey); Draw(rp, ex, ey);
        Move(rp, sx, sy); Draw(rp, sx, ey);
        Move(rp, ex, sy); Draw(rp, ex, ey);
        RectFill(rp, sx + 5, sy + 3, sx + 10, sy + 4);
        RectFill(rp, sx + 9, sy + 5, sx + 10, sy + 7);
        RectFill(rp, sx + 6, sy + 7, sx + 9, sy + 8);
        RectFill(rp, sx + 6, sy + 9, sx + 7, sy + 10);
        RectFill(rp, sx + 6, sy + 12, sx + 7, sy + 13);
        break;

    case TILE_QBLOCK_HIT:
        SetAPen(rp, COL_GREY);
        RectFill(rp, sx, sy, ex, ey);
        SetAPen(rp, COL_DKBROWN);
        Move(rp, sx, sy); Draw(rp, ex, sy);
        Move(rp, sx, ey); Draw(rp, ex, ey);
        Move(rp, sx, sy); Draw(rp, sx, ey);
        Move(rp, ex, sy); Draw(rp, ex, ey);
        break;

    case TILE_STONE:
        SetAPen(rp, COL_GREY);
        RectFill(rp, sx, sy, ex, ey);
        SetAPen(rp, COL_BLACK);
        Move(rp, sx, sy); Draw(rp, ex, sy);
        Move(rp, sx, ey); Draw(rp, ex, ey);
        Move(rp, sx, sy); Draw(rp, sx, ey);
        Move(rp, ex, sy); Draw(rp, ex, ey);
        SetAPen(rp, COL_WHITE);
        Move(rp, sx + 1, sy + 1); Draw(rp, ex - 1, sy + 1);
        Move(rp, sx + 1, sy + 1); Draw(rp, sx + 1, ey - 1);
        break;

    case TILE_PIPE_TL:
        SetAPen(rp, COL_DKGREEN);
        RectFill(rp, sx, sy, ex, ey);
        SetAPen(rp, COL_GREEN);
        RectFill(rp, sx + 2, sy + 2, ex - 1, sy + 4);
        SetAPen(rp, COL_LTGREEN);
        RectFill(rp, sx + 3, sy + 2, sx + 5, sy + 4);
        SetAPen(rp, COL_GREEN);
        RectFill(rp, sx + 2, sy + 5, ex, ey);
        break;

    case TILE_PIPE_TR:
        SetAPen(rp, COL_DKGREEN);
        RectFill(rp, sx, sy, ex, ey);
        SetAPen(rp, COL_GREEN);
        RectFill(rp, sx, sy + 2, ex - 2, sy + 4);
        RectFill(rp, sx, sy + 5, ex - 2, ey);
        break;

    case TILE_PIPE_BL:
        SetAPen(rp, COL_SKY);
        RectFill(rp, sx, sy, sx + 1, ey);
        SetAPen(rp, COL_GREEN);
        RectFill(rp, sx + 2, sy, ex, ey);
        SetAPen(rp, COL_DKGREEN);
        Move(rp, sx + 2, sy); Draw(rp, sx + 2, ey);
        break;

    case TILE_PIPE_BR:
        SetAPen(rp, COL_SKY);
        RectFill(rp, ex - 1, sy, ex, ey);
        SetAPen(rp, COL_GREEN);
        RectFill(rp, sx, sy, ex - 2, ey);
        SetAPen(rp, COL_DKGREEN);
        Move(rp, ex - 2, sy); Draw(rp, ex - 2, ey);
        break;

    case TILE_PLATFORM:
        SetAPen(rp, COL_SKY);
        RectFill(rp, sx, sy, ex, ey);
        SetAPen(rp, COL_BROWN);
        RectFill(rp, sx, sy, ex, sy + 3);
        SetAPen(rp, COL_DKBROWN);
        Move(rp, sx, sy + 3); Draw(rp, ex, sy + 3);
        break;

    case TILE_CLOUD_L:
        SetAPen(rp, COL_SKY);
        RectFill(rp, sx, sy, ex, ey);
        SetAPen(rp, COL_WHITE);
        RectFill(rp, sx + 4, sy + 4, ex, sy + 12);
        RectFill(rp, sx + 8, sy + 2, ex, sy + 4);
        break;

    case TILE_CLOUD_R:
        SetAPen(rp, COL_SKY);
        RectFill(rp, sx, sy, ex, ey);
        SetAPen(rp, COL_WHITE);
        RectFill(rp, sx, sy + 4, sx + 12, sy + 12);
        RectFill(rp, sx, sy + 2, sx + 8, sy + 4);
        break;

    case TILE_FLAG:
        SetAPen(rp, COL_SKY);
        RectFill(rp, sx, sy, ex, ey);
        SetAPen(rp, COL_GREY);
        RectFill(rp, sx + 7, sy, sx + 8, ey);
        SetAPen(rp, COL_RED);
        RectFill(rp, sx + 9, sy + 1, sx + 15, sy + 6);
        SetAPen(rp, COL_YELLOW);
        RectFill(rp, sx + 6, sy, sx + 9, sy + 1);
        break;

    case TILE_BUSH:
        SetAPen(rp, COL_SKY);
        RectFill(rp, sx, sy, ex, ey);
        SetAPen(rp, COL_GREEN);
        RectFill(rp, sx + 2, sy + 6, sx + 14, sy + 14);
        RectFill(rp, sx + 4, sy + 4, sx + 12, sy + 6);
        SetAPen(rp, COL_DKGREEN);
        RectFill(rp, sx + 6, sy + 8, sx + 10, sy + 10);
        break;

    default:
        SetAPen(rp, COL_SKY);
        RectFill(rp, sx, sy, ex, ey);
        break;
    }
}

void gfx_clear_area(struct RastPort *rp, int x, int y, int w, int h, int color) {
    SetAPen(rp, color);
    RectFill(rp, x, y, x + w - 1, y + h - 1);
}
