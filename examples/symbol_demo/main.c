/*
 * Symbol Demo - Example program with rich debug symbols for testing
 * the Symbol Table / Debug Info Loading system.
 *
 * Compiled with -g for STABS debug info. Exercises:
 * - Multiple functions at known addresses
 * - Structs with named fields
 * - Global and local variables
 * - Nested function calls for stack traces
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#include "bridge_client.h"

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;

/* ---- Data structures for symbol inspection ---- */

typedef struct {
    LONG x;
    LONG y;
} Vec2;

typedef struct {
    Vec2 position;
    Vec2 velocity;
    LONG width;
    LONG height;
    UBYTE color;
    UBYTE active;
    char name[32];
} Entity;

typedef struct {
    LONG score;
    LONG level;
    LONG lives;
    LONG frame_count;
    Entity player;
    Entity enemies[4];
} GameState;

/* ---- Globals for variable inspection ---- */

static GameState game;
static LONG running = 1;
static LONG tick = 0;

/* ---- Functions at distinct addresses for symbol testing ---- */

static void init_entity(Entity *e, const char *name, LONG x, LONG y)
{
    e->position.x = x;
    e->position.y = y;
    e->velocity.x = 0;
    e->velocity.y = 0;
    e->width = 16;
    e->height = 16;
    e->color = 1;
    e->active = 1;
    strncpy(e->name, name, 31);
    e->name[31] = '\0';
}

static void init_game(GameState *gs)
{
    int i;
    char buf[32];

    gs->score = 0;
    gs->level = 1;
    gs->lives = 3;
    gs->frame_count = 0;

    init_entity(&gs->player, "Player", 160, 200);
    gs->player.color = 3;

    for (i = 0; i < 4; i++) {
        sprintf(buf, "Enemy_%ld", (long)i);
        init_entity(&gs->enemies[i], buf, 40 + i * 60, 30);
        gs->enemies[i].velocity.x = (i & 1) ? 2 : -2;
        gs->enemies[i].velocity.y = 1;
        gs->enemies[i].color = 2;
    }

    AB_I("Game initialized: level=%ld lives=%ld", (long)gs->level, (long)gs->lives);
}

static void move_entity(Entity *e, LONG min_x, LONG max_x, LONG min_y, LONG max_y)
{
    if (!e->active) return;

    e->position.x += e->velocity.x;
    e->position.y += e->velocity.y;

    /* Bounce off walls */
    if (e->position.x < min_x) {
        e->position.x = min_x;
        e->velocity.x = -e->velocity.x;
    }
    if (e->position.x > max_x - e->width) {
        e->position.x = max_x - e->width;
        e->velocity.x = -e->velocity.x;
    }
    if (e->position.y < min_y) {
        e->position.y = min_y;
        e->velocity.y = -e->velocity.y;
    }
    if (e->position.y > max_y - e->height) {
        e->position.y = max_y - e->height;
        e->velocity.y = -e->velocity.y;
    }
}

static LONG check_collision(const Entity *a, const Entity *b)
{
    if (!a->active || !b->active) return 0;

    if (a->position.x < b->position.x + b->width &&
        a->position.x + a->width > b->position.x &&
        a->position.y < b->position.y + b->height &&
        a->position.y + a->height > b->position.y)
    {
        return 1;
    }
    return 0;
}

static void update_game(GameState *gs)
{
    int i;

    gs->frame_count++;

    /* Move enemies */
    for (i = 0; i < 4; i++) {
        move_entity(&gs->enemies[i], 0, 300, 0, 200);
    }

    /* Check collisions */
    for (i = 0; i < 4; i++) {
        if (check_collision(&gs->player, &gs->enemies[i])) {
            gs->lives--;
            AB_W("Collision with %s! Lives=%ld", gs->enemies[i].name, (long)gs->lives);

            if (gs->lives <= 0) {
                AB_E("Game Over! Final score: %ld", (long)gs->score);
                running = 0;
                return;
            }

            /* Reset player position */
            gs->player.position.x = 160;
            gs->player.position.y = 200;
        }
    }

    /* Score increases with time */
    if ((gs->frame_count % 50) == 0) {
        gs->score += 10;
    }

    /* Level up every 500 points */
    if (gs->score > 0 && (gs->score % 500) == 0 && gs->score != gs->level * 500) {
        gs->level++;
        AB_I("Level up! Now level %ld", (long)gs->level);
        /* Speed up enemies */
        for (i = 0; i < 4; i++) {
            if (gs->enemies[i].velocity.x > 0) gs->enemies[i].velocity.x++;
            else gs->enemies[i].velocity.x--;
        }
    }
}

static void draw_entity(struct RastPort *rp, const Entity *e)
{
    if (!e->active) return;
    SetAPen(rp, e->color);
    RectFill(rp, e->position.x, e->position.y,
             e->position.x + e->width - 1,
             e->position.y + e->height - 1);
}

static void render_game(struct RastPort *rp, const GameState *gs)
{
    int i;
    char status[80];

    /* Clear play area */
    SetAPen(rp, 0);
    RectFill(rp, 0, 12, 299, 179);

    /* Draw entities */
    draw_entity(rp, &gs->player);
    for (i = 0; i < 4; i++) {
        draw_entity(rp, &gs->enemies[i]);
    }

    /* Draw status line */
    sprintf(status, "Score:%ld Lv:%ld Lives:%ld Frame:%ld",
            (long)gs->score, (long)gs->level, (long)gs->lives, (long)gs->frame_count);
    SetAPen(rp, 0);
    RectFill(rp, 0, 180, 299, 192);
    SetAPen(rp, 1);
    Move(rp, 4, 190);
    Text(rp, status, strlen(status));
}

/* Hook handler for remote control */
static int move_hook(const char *args, char *resultBuf, int bufSize)
{
    if (!args) return 1;

    if (strcmp(args, "left") == 0) {
        game.player.velocity.x = -3;
    } else if (strcmp(args, "right") == 0) {
        game.player.velocity.x = 3;
    } else if (strcmp(args, "up") == 0) {
        game.player.velocity.y = -3;
    } else if (strcmp(args, "down") == 0) {
        game.player.velocity.y = 3;
    } else if (strcmp(args, "stop") == 0) {
        game.player.velocity.x = 0;
        game.player.velocity.y = 0;
    }

    AB_D("Player move: %s -> vel(%ld,%ld)", args,
         (long)game.player.velocity.x, (long)game.player.velocity.y);

    sprintf(resultBuf, "vel=%ld,%ld", (long)game.player.velocity.x, (long)game.player.velocity.y);
    return 0;
}

int main(void)
{
    struct Window *win;
    struct IntuiMessage *msg;
    ULONG class;
    int hb_counter = 0;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 36);
    if (!IntuitionBase) return 1;

    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 36);
    if (!GfxBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    printf("symbol_demo starting\n");

    if (ab_init("symbol_demo") != 0) {
        printf("  Bridge: NOT FOUND\n");
    } else {
        printf("  Bridge: CONNECTED\n");
    }

    /* Register variables */
    ab_register_var("score", AB_TYPE_I32, &game.score);
    ab_register_var("level", AB_TYPE_I32, &game.level);
    ab_register_var("lives", AB_TYPE_I32, &game.lives);
    ab_register_var("frame", AB_TYPE_I32, &game.frame_count);
    ab_register_var("running", AB_TYPE_I32, &running);
    ab_register_var("player_x", AB_TYPE_I32, &game.player.position.x);
    ab_register_var("player_y", AB_TYPE_I32, &game.player.position.y);

    /* Register hooks */
    ab_register_hook("move", "Move player: left/right/up/down/stop", move_hook);

    /* Register memory regions for struct inspection */
    ab_register_memregion("game_state", &game, sizeof(GameState), "Full game state struct");
    ab_register_memregion("player", &game.player, sizeof(Entity), "Player entity");

    init_game(&game);

    win = OpenWindowTags(NULL,
        WA_Left, 10,
        WA_Top, 10,
        WA_Width, 300,
        WA_Height, 200,
        WA_Title, (ULONG)"Symbol Demo [Bridge]",
        WA_CloseGadget, TRUE,
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW,
        WA_Activate, TRUE,
        TAG_DONE);

    if (!win) {
        AB_E("Failed to open window");
        ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }

    AB_I("Symbol Demo started, window open");

    while (running) {
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            class = msg->Class;
            ReplyMsg((struct Message *)msg);
            if (class == IDCMP_CLOSEWINDOW) {
                AB_I("Close window requested");
                running = 0;
            }
        }

        tick++;
        update_game(&game);
        render_game(win->RPort, &game);

        if ((++hb_counter % 250) == 0) {
            ab_heartbeat();
        }

        ab_poll();
        move_entity(&game.player, 0, 300, 0, 180);

        Delay(2);  /* ~25fps */
    }

    AB_I("Symbol Demo shutting down, score=%ld", (long)game.score);

    CloseWindow(win);
    ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    return 0;
}
