/*
 * App Launcher for Amiga
 *
 * GadTools ListView window on Workbench screen.
 * Scans DH2:Dev/ for executables, sorts by:
 *   1. New apps first (not seen in previous runs)
 *   2. Then by launch count (most used first)
 * Saves launch stats to DH2:Dev/launcher.stats
 *
 * Controls:
 *   Click         Select app
 *   Double-click  Launch app
 *   Up/Down       Navigate list
 *   Enter         Launch selected
 *   ESC           Quit
 */

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <libraries/gadtools.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <hardware/custom.h>
#include <hardware/cia.h>
#include <dos/dos.h>
#include <exec/memory.h>
#include <exec/lists.h>
#include <exec/interrupts.h>
#include <dos/dostags.h>
#include <string.h>

char *sprintf(char *buf, const char *fmt, ...);

/* Libraries */
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase       *GfxBase = NULL;
struct Library       *GadToolsBase = NULL;

/* Paths */
#define APP_DIR     "DH2:Dev/"
#define STATS_FILE  "DH2:Dev/launcher.stats"

#define MAX_APPS    64
#define NAME_LEN    40
#define DISP_LEN    80

#define WIN_W       320
#define WIN_H       300
#define WIN_LEFT    10
#define WIN_TOP     15

#define GID_LIST    1
#define GID_LAUNCH  2
#define GID_QUIT    3

/* Skip these extensions */
static const char *skip_ext[] = {
    ".mod", ".stats", ".scores", ".info", ".readme", ".txt",
    ".md", ".h", ".c", ".o", ".asm", ".png", ".iff", ".toml",
    ".json", ".a", ".sh", ".py", ".dat", ".raw", ".lha",
    ".cfg", ".prefs", ".bak", ".tmp", ".log", ".map", NULL
};

/* Skip these exact names */
static const char *skip_names[] = {
    "launcher", "amiga-bridge", "amiga-bridge2", "closescr",
    "shell_proxy", "debug_test", "test_example", "test_new_features",
    "foo", "sfx_player", "symbol_demo", "disk_benchmark",
    NULL
};

/* ---- Input handler for keyboard (bypasses screen focus issues) ---- */
static volatile UBYTE key_pressed[128];  /* edge-triggered: set on press, cleared after read */
static volatile UBYTE key_held[128];

static struct MsgPort *inp_port = NULL;
static struct IOStdReq *inp_req = NULL;
static struct Interrupt inp_handler;
static WORD inp_installed = 0;

static struct InputEvent * __attribute__((used))
key_handler_func(void)
{
    register struct InputEvent *events __asm("a0");
    struct InputEvent *ev;
    for (ev = events; ev; ev = ev->ie_NextEvent) {
        if (ev->ie_Class == IECLASS_RAWKEY) {
            UWORD code = ev->ie_Code;
            UWORD key = code & 0x7F;
            if (key < 128) {
                if (code & 0x80) {
                    key_held[key] = 0;
                } else {
                    key_held[key] = 1;
                    key_pressed[key] = 1;
                }
            }
        }
    }
    return events;
}

static void input_handler_init(void)
{
    WORD i;
    for (i = 0; i < 128; i++) { key_pressed[i] = 0; key_held[i] = 0; }

    inp_port = CreateMsgPort();
    if (!inp_port) return;
    inp_req = (struct IOStdReq *)CreateIORequest(inp_port, sizeof(struct IOStdReq));
    if (!inp_req) return;
    if (OpenDevice((STRPTR)"input.device", 0, (struct IORequest *)inp_req, 0) != 0) {
        DeleteIORequest((struct IORequest *)inp_req);
        inp_req = NULL;
        return;
    }
    inp_handler.is_Node.ln_Type = NT_INTERRUPT;
    inp_handler.is_Node.ln_Pri = 100;
    inp_handler.is_Node.ln_Name = (char *)"LauncherKeys";
    inp_handler.is_Data = NULL;
    inp_handler.is_Code = (void (*)())key_handler_func;
    inp_req->io_Command = IND_ADDHANDLER;
    inp_req->io_Data = (APTR)&inp_handler;
    DoIO((struct IORequest *)inp_req);
    inp_installed = 1;
}

static void input_handler_cleanup(void)
{
    if (inp_installed && inp_req) {
        inp_req->io_Command = IND_REMHANDLER;
        inp_req->io_Data = (APTR)&inp_handler;
        DoIO((struct IORequest *)inp_req);
        inp_installed = 0;
    }
    if (inp_req) {
        CloseDevice((struct IORequest *)inp_req);
        DeleteIORequest((struct IORequest *)inp_req);
        inp_req = NULL;
    }
    if (inp_port) { DeleteMsgPort(inp_port); inp_port = NULL; }
}

/* Consume a key press (edge-triggered) */
static WORD key_pop(UBYTE key)
{
    if (key_pressed[key]) { key_pressed[key] = 0; return 1; }
    return 0;
}

/* ---- App entry ---- */
typedef struct {
    struct Node node;
    char name[NAME_LEN];
    char display[DISP_LEN];
    LONG count;
    WORD is_new;
} AppEntry;

static AppEntry apps[MAX_APPS];
static WORD app_count = 0;
static struct List app_list;
static WORD selected = 0;

/* ---- Stats ---- */

static void stats_load(void)
{
    BPTR fh;
    char buf[512];
    LONG len;
    char *p;

    fh = Open((STRPTR)STATS_FILE, MODE_OLDFILE);
    if (!fh) return;
    len = Read(fh, buf, 511);
    Close(fh);
    if (len <= 0) return;
    buf[len] = 0;

    p = buf;
    while (*p) {
        char name[NAME_LEN];
        LONG count = 0;
        WORD ni = 0;

        while (*p && *p != ' ' && *p != '\n' && ni < NAME_LEN - 1)
            name[ni++] = *p++;
        name[ni] = 0;
        while (*p == ' ') p++;
        while (*p >= '0' && *p <= '9') { count = count * 10 + (*p - '0'); p++; }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;

        if (ni > 0) {
            WORD i, found = 0;
            for (i = 0; i < app_count; i++) {
                if (strcmp(apps[i].name, name) == 0) {
                    apps[i].count = count;
                    found = 1;
                    break;
                }
            }
            if (!found && app_count < MAX_APPS) {
                strncpy(apps[app_count].name, name, NAME_LEN - 1);
                apps[app_count].count = count;
                apps[app_count].is_new = 0;
                app_count++;
            }
        }
    }
}

static void stats_save(void)
{
    BPTR fh;
    WORD i;
    char buf[64];

    fh = Open((STRPTR)STATS_FILE, MODE_NEWFILE);
    if (!fh) return;
    for (i = 0; i < app_count; i++) {
        LONG slen;
        sprintf(buf, "%s %ld\n", apps[i].name, (long)apps[i].count);
        slen = strlen(buf);
        Write(fh, buf, slen);
    }
    Close(fh);
}

/* ---- Directory scanning ---- */

static WORD has_skip_extension(const char *name)
{
    WORD i, nlen = strlen(name);
    for (i = 0; skip_ext[i]; i++) {
        WORD elen = strlen(skip_ext[i]);
        if (nlen > elen) {
            const char *tail = name + nlen - elen;
            WORD j, match = 1;
            for (j = 0; j < elen; j++) {
                char a = tail[j], b = skip_ext[i][j];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) { match = 0; break; }
            }
            if (match) return 1;
        }
    }
    return 0;
}

static void scan_directory(void)
{
    BPTR lock;
    struct FileInfoBlock *fib;

    lock = Lock((STRPTR)APP_DIR, SHARED_LOCK);
    if (!lock) return;

    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (!fib) { UnLock(lock); return; }

    if (Examine(lock, fib)) {
        while (ExNext(lock, fib)) {
            if (fib->fib_DirEntryType > 0) continue;

            /* Skip known non-app names */
            {
                WORD skip = 0, si;
                for (si = 0; skip_names[si]; si++) {
                    if (strcmp(fib->fib_FileName, skip_names[si]) == 0)
                        { skip = 1; break; }
                }
                if (skip) continue;
            }

            if (has_skip_extension(fib->fib_FileName)) continue;
            if (fib->fib_Size < 4096) continue;

            /* Check if already in list from stats */
            {
                WORD i, found = 0;
                for (i = 0; i < app_count; i++) {
                    if (strcmp(apps[i].name, fib->fib_FileName) == 0)
                        { found = 1; break; }
                }
                if (!found && app_count < MAX_APPS) {
                    strncpy(apps[app_count].name, fib->fib_FileName, NAME_LEN - 1);
                    apps[app_count].count = 0;
                    apps[app_count].is_new = 1;
                    app_count++;
                }
            }
        }
    }

    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);

    /* Remove entries for files that no longer exist */
    {
        WORD i;
        char path[80];
        for (i = 0; i < app_count; i++) {
            BPTR test;
            sprintf(path, APP_DIR "%s", apps[i].name);
            test = Lock((STRPTR)path, SHARED_LOCK);
            if (!test) {
                WORD j;
                for (j = i; j < app_count - 1; j++) apps[j] = apps[j + 1];
                app_count--;
                i--;
            } else {
                UnLock(test);
            }
        }
    }
}

/* ---- Sort ---- */

static void sort_apps(void)
{
    WORD i, j;
    for (i = 1; i < app_count; i++) {
        AppEntry tmp = apps[i];
        j = i - 1;
        while (j >= 0) {
            WORD swap = 0;
            if (tmp.is_new && !apps[j].is_new)
                swap = 1;
            else if (tmp.is_new && apps[j].is_new) {
                if (strcmp(tmp.name, apps[j].name) < 0) swap = 1;
            } else if (!tmp.is_new && !apps[j].is_new) {
                if (tmp.count > apps[j].count) swap = 1;
            }
            if (!swap) break;
            apps[j + 1] = apps[j];
            j--;
        }
        apps[j + 1] = tmp;
    }
}

/* ---- Build display list ---- */

static void build_list(void)
{
    WORD i;

    app_list.lh_Head = (struct Node *)&app_list.lh_Tail;
    app_list.lh_Tail = NULL;
    app_list.lh_TailPred = (struct Node *)&app_list.lh_Head;

    for (i = 0; i < app_count; i++) {
        WORD dlen;

        sprintf(apps[i].display, "%s", apps[i].name);
        dlen = strlen(apps[i].display);

        /* Pad to column 32, then add tag */
        while (dlen < 32) apps[i].display[dlen++] = ' ';
        apps[i].display[dlen] = 0;

        if (apps[i].is_new) {
            strcat(apps[i].display, "[NEW]");
        } else if (apps[i].count > 0) {
            char cnt[16];
            sprintf(cnt, "(%ld)", (long)apps[i].count);
            strcat(apps[i].display, cnt);
        }

        apps[i].node.ln_Name = apps[i].display;
        apps[i].node.ln_Type = 0;
        apps[i].node.ln_Pri = 0;
        AddTail(&app_list, &apps[i].node);
    }
}

/* ---- Launch ---- */

static void launch_app(WORD idx)
{
    char cmd[120];
    BPTR nil;
    if (idx < 0 || idx >= app_count) return;

    apps[idx].count++;
    apps[idx].is_new = 0;
    stats_save();

    /* Use SystemTagList for clean async launch (no signal inheritance) */
    sprintf(cmd, APP_DIR "%s", apps[idx].name);
    nil = Open((STRPTR)"NIL:", MODE_OLDFILE);
    SystemTags((STRPTR)cmd,
        SYS_Asynch, TRUE,
        SYS_Input,  nil ? nil : 0,
        SYS_Output, 0,
        NP_StackSize, 16384,
        TAG_DONE);
    /* SystemTags closes nil handle when async */
}

/* ---- Main ---- */

int main(int argc, char *argv[])
{
    struct Screen *wb_screen = NULL;
    APTR vi = NULL;
    struct Gadget *glist = NULL, *gad;
    struct Gadget *list_gad = NULL;
    struct Window *win = NULL;
    struct NewGadget ng;
    WORD running = 1;
    WORD top_edge;
    ULONG last_click_secs = 0, last_click_micros = 0;
    WORD last_click_item = -1;
    WORD key_repeat_delay = 0;

    (void)argc; (void)argv;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39);
    GadToolsBase = OpenLibrary((STRPTR)"gadtools.library", 39);
    if (!IntuitionBase || !GfxBase || !GadToolsBase) goto cleanup;

    /* Install keyboard handler */
    input_handler_init();

    /* Load stats, scan, sort, build list */
    stats_load();
    scan_directory();
    sort_apps();
    build_list();

    /* GadTools setup on Workbench screen */
    wb_screen = LockPubScreen(NULL);
    if (!wb_screen) goto cleanup;

    vi = GetVisualInfoA(wb_screen, NULL);
    if (!vi) { UnlockPubScreen(NULL, wb_screen); goto cleanup; }

    top_edge = wb_screen->WBorTop + wb_screen->Font->ta_YSize + 1;

    gad = CreateContext(&glist);
    if (!gad) { UnlockPubScreen(NULL, wb_screen); goto cleanup; }

    /* ListView */
    memset(&ng, 0, sizeof(ng));
    ng.ng_LeftEdge = 8;
    ng.ng_TopEdge = top_edge + 4;
    ng.ng_Width = WIN_W - 16;
    ng.ng_Height = WIN_H - top_edge - 40;
    ng.ng_GadgetText = NULL;
    ng.ng_GadgetID = GID_LIST;
    ng.ng_VisualInfo = vi;

    list_gad = gad = CreateGadget(LISTVIEW_KIND, gad, &ng,
        GTLV_Labels,       &app_list,
        GTLV_ShowSelected, NULL,
        GTLV_Selected,     0,
        TAG_DONE);

    /* Launch button */
    ng.ng_TopEdge = WIN_H - 28;
    ng.ng_Width = 120;
    ng.ng_Height = 20;
    ng.ng_LeftEdge = 8;
    ng.ng_GadgetText = (UBYTE *)"Launch";
    ng.ng_GadgetID = GID_LAUNCH;
    gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);

    /* Quit button */
    ng.ng_LeftEdge = WIN_W - 128;
    ng.ng_GadgetText = (UBYTE *)"Quit";
    ng.ng_GadgetID = GID_QUIT;
    gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);

    if (!gad) { UnlockPubScreen(NULL, wb_screen); goto cleanup; }

    win = OpenWindowTags(NULL,
        WA_Left,        WIN_LEFT,
        WA_Top,         WIN_TOP,
        WA_Width,       WIN_W,
        WA_Height,      WIN_H,
        WA_Title,       (ULONG)"Launcher  W/S=Nav Enter=Run",
        WA_PubScreen,   (ULONG)wb_screen,
        WA_Gadgets,     (ULONG)glist,
        WA_DragBar,     TRUE,
        WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE,
        WA_Activate,    TRUE,
        WA_SmartRefresh,TRUE,
        WA_IDCMP,       IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW | IDCMP_INTUITICKS,
        TAG_DONE);

    UnlockPubScreen(NULL, wb_screen);
    wb_screen = NULL;

    if (!win) goto cleanup;
    GT_RefreshWindow(win, NULL);

    /* ---- Event loop ---- */
    while (running) {
        struct IntuiMessage *imsg;

        Wait(1L << win->UserPort->mp_SigBit);

        while ((imsg = GT_GetIMsg(win->UserPort))) {
            ULONG cl = imsg->Class;
            UWORD code = imsg->Code;
            struct Gadget *igad = (struct Gadget *)imsg->IAddress;
            ULONG secs = imsg->Seconds;
            ULONG micros = imsg->Micros;

            GT_ReplyIMsg(imsg);

            switch (cl) {
            case IDCMP_CLOSEWINDOW:
                running = 0;
                break;

            case IDCMP_REFRESHWINDOW:
                GT_BeginRefresh(win);
                GT_EndRefresh(win, TRUE);
                break;

            case IDCMP_GADGETUP:
                if (igad->GadgetID == GID_LIST) {
                    selected = code;
                    /* Double-click detection */
                    if (selected == last_click_item && DoubleClick(last_click_secs, last_click_micros, secs, micros)) {
                        launch_app(selected);
                        last_click_item = -1;
                    } else {
                        last_click_secs = secs;
                        last_click_micros = micros;
                        last_click_item = selected;
                    }
                } else if (igad->GadgetID == GID_LAUNCH) {
                    launch_app(selected);
                } else if (igad->GadgetID == GID_QUIT) {
                    running = 0;
                }
                break;

            case IDCMP_INTUITICKS:
            {
                extern volatile struct Custom custom;
                extern volatile struct CIA ciaa;
                WORD want_up = 0, want_down = 0, want_launch = 0;
                UWORD joy;

                /* ESC does nothing - use close gadget or Quit button */

                /* Enter to launch */
                if (key_pop(0x44) || key_pop(0x43)) want_launch = 1;

                /* Keyboard: W/Up = up, S/Down = down */
                if (key_pop(0x11) || key_pop(0x4C)) want_up = 1;
                if (key_pop(0x21) || key_pop(0x4D)) want_down = 1;

                /* Held key repeat for W/S/arrows */
                if (key_held[0x11] || key_held[0x4C] ||
                    key_held[0x21] || key_held[0x4D]) {
                    if (key_repeat_delay > 0) {
                        key_repeat_delay--;
                    } else {
                        if (key_held[0x11] || key_held[0x4C]) want_up = 1;
                        if (key_held[0x21] || key_held[0x4D]) want_down = 1;
                        key_repeat_delay = 2;
                    }
                } else {
                    key_repeat_delay = 0;
                }

                /* Joystick port 2 - covers arrow keys mapped to joystick in FS-UAE */
                joy = custom.joy1dat;
                {
                    UWORD v = (joy >> 8) & 3;
                    if (v == 1) want_up = 1;    /* joy up */
                    if (v == 2) want_down = 1;  /* joy down */
                }
                /* Joy fire = launch */
                if (!(ciaa.ciapra & 0x80)) want_launch = 1;

                /* Apply navigation */
                if (want_up && selected > 0) {
                    selected--;
                    GT_SetGadgetAttrs(list_gad, win, NULL,
                        GTLV_Selected, (ULONG)selected,
                        GTLV_MakeVisible, (ULONG)selected,
                        TAG_DONE);
                }
                if (want_down && selected < app_count - 1) {
                    selected++;
                    GT_SetGadgetAttrs(list_gad, win, NULL,
                        GTLV_Selected, (ULONG)selected,
                        GTLV_MakeVisible, (ULONG)selected,
                        TAG_DONE);
                }
                if (want_launch) {
                    launch_app(selected);
                }
                break;
            }
            }
        }
    }

cleanup:
    input_handler_cleanup();
    if (win) CloseWindow(win);
    if (glist) FreeGadgets(glist);
    if (vi) FreeVisualInfo(vi);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    return 0;
}
