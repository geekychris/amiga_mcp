/*
 * main.c - AmigaBridge daemon
 *
 * Central daemon that manages serial communication to host,
 * IPC with client applications, and provides a status window.
 *
 * Uses async serial I/O with signal-based Wait() loop.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <intuition/intuition.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>

#include <string.h>
#include <stdio.h>

#include "bridge_internal.h"

extern struct ExecBase *SysBase;
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;

/* UI state - global, accessed by other modules */
char g_ui_logs[UI_MAX_LOG_LINES][UI_MAX_LOG_LEN];
int g_ui_log_head = 0;
BOOL g_ui_dirty = TRUE;
BOOL g_serial_connected = FALSE;
BOOL g_host_connected = FALSE;

/* Serial line buffer */
static char line_buf[BRIDGE_MAX_LINE];
static int line_pos = 0;

/* Heartbeat timer */
static ULONG hb_tick = 0;
static ULONG hb_counter = 0;
#define HB_INTERVAL 250  /* ~5 seconds at 50Hz timer */

/* Window */
static struct Window *win = NULL;

/* Timer for periodic wake-up (snoop drain, heartbeat) */
static struct MsgPort *timerPort = NULL;
static struct timerequest *timerReq = NULL;
static BOOL timerOpen = FALSE;
static ULONG timerSig = 0;

/* Window dimensions */
#define WIN_WIDTH  310
#define WIN_HEIGHT 160
#define WIN_LEFT   10
#define WIN_TOP    20

/* Text line positions (in GimmeZeroZero inner area) */
#define TEXT_LEFT    6
#define TEXT_TOP     10
#define TEXT_LINEH   12

static void open_libs(void)
{
    IntuitionBase = (struct IntuitionBase *)OpenLibrary(
        (CONST_STRPTR)"intuition.library", 36);
    GfxBase = (struct GfxBase *)OpenLibrary(
        (CONST_STRPTR)"graphics.library", 36);
}

static void close_libs(void)
{
    if (GfxBase) {
        CloseLibrary((struct Library *)GfxBase);
        GfxBase = NULL;
    }
    if (IntuitionBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
    }
}

static struct Window *open_window(void)
{
    struct NewWindow nw;

    memset(&nw, 0, sizeof(nw));
    nw.LeftEdge = WIN_LEFT;
    nw.TopEdge = WIN_TOP;
    nw.Width = WIN_WIDTH;
    nw.Height = WIN_HEIGHT;
    nw.DetailPen = 0;
    nw.BlockPen = 1;
    nw.Title = (UBYTE *)"AmigaBridge v1.0";
    nw.Flags = WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
               WFLG_ACTIVATE | WFLG_SMART_REFRESH |
               WFLG_GIMMEZEROZERO;
    nw.IDCMPFlags = IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW;
    nw.Type = WBENCHSCREEN;
    nw.MinWidth = WIN_WIDTH;
    nw.MinHeight = WIN_HEIGHT;
    nw.MaxWidth = WIN_WIDTH;
    nw.MaxHeight = WIN_HEIGHT;

    return OpenWindow(&nw);
}

static void draw_text_line(struct RastPort *rp, int lineNum,
                           const char *text)
{
    int x = TEXT_LEFT;
    int y = TEXT_TOP + lineNum * TEXT_LINEH;
    int len = strlen(text);
    int maxChars = 38; /* approx chars that fit in window */

    if (len > maxChars) len = maxChars;

    /* Clear the line area */
    SetAPen(rp, 0);
    RectFill(rp, 0, y - 7, WIN_WIDTH - 20, y + 3);

    /* Draw text */
    SetAPen(rp, 1);
    Move(rp, x, y);
    Text(rp, (CONST_STRPTR)text, len);
}

static void ui_redraw(void)
{
    struct RastPort *rp;
    char linebuf[48];
    int i;
    int logIdx;

    if (!win) return;

    rp = win->RPort;

    /* Line 0: Serial status */
    if (g_serial_connected) {
        draw_text_line(rp, 0, "Serial: Connected");
    } else {
        draw_text_line(rp, 0, "Serial: Disconnected");
    }

    /* Line 1: Host status */
    if (g_host_connected) {
        draw_text_line(rp, 1, "Host: Connected");
    } else {
        draw_text_line(rp, 1, "Host: Waiting...");
    }

    /* Line 2: Client count */
    sprintf(linebuf, "Clients: %ld", (long)client_count());
    draw_text_line(rp, 2, linebuf);

    /* Lines 3-7: Last 5 log messages */
    for (i = 0; i < UI_MAX_LOG_LINES; i++) {
        logIdx = (g_ui_log_head + i) % UI_MAX_LOG_LINES;
        if (g_ui_logs[logIdx][0] != '\0') {
            draw_text_line(rp, 3 + i, g_ui_logs[logIdx]);
        } else {
            draw_text_line(rp, 3 + i, "");
        }
    }

    /* Line 8: Message counters */
    sprintf(linebuf, "Msgs: TX:%lu RX:%lu",
            (unsigned long)g_tx_count,
            (unsigned long)g_rx_count);
    draw_text_line(rp, 8, linebuf);

    g_ui_dirty = FALSE;
}

void ui_add_log(const char *msg)
{
    strncpy(g_ui_logs[g_ui_log_head], msg, UI_MAX_LOG_LEN - 1);
    g_ui_logs[g_ui_log_head][UI_MAX_LOG_LEN - 1] = '\0';
    g_ui_log_head = (g_ui_log_head + 1) % UI_MAX_LOG_LINES;
    g_ui_dirty = TRUE;
}

/*
 * Process a completed serial line.
 */
static void process_serial_line(void)
{
    if (line_pos == 0) return;
    line_buf[line_pos] = '\0';
    protocol_parse_line(line_buf);
    line_pos = 0;
}

/*
 * Handle a byte received from serial.
 */
static void handle_serial_byte(char ch)
{
    if (ch == '\n') {
        process_serial_line();
    } else if (ch == '\r') {
        /* Ignore CR */
    } else {
        if (line_pos < BRIDGE_MAX_LINE - 1) {
            line_buf[line_pos++] = ch;
        }
    }
}

/*
 * Send periodic heartbeat to host.
 */
static void send_heartbeat(void)
{
    ULONG chip, fast;
    sys_avail_mem(&chip, &fast);
    protocol_send_heartbeat(hb_tick++, chip, fast);
}

int main(int argc, char **argv)
{
    BOOL running = TRUE;
    ULONG serialSig, ipcSig, winSig, signals, received;
    int i;

    /* Initialize UI log buffer */
    for (i = 0; i < UI_MAX_LOG_LINES; i++) {
        g_ui_logs[i][0] = '\0';
    }

    /* Open libraries */
    open_libs();
    if (!IntuitionBase || !GfxBase) {
        if (IntuitionBase || GfxBase) close_libs();
        return 20;
    }

    /* Open status window */
    win = open_window();
    if (!win) {
        close_libs();
        return 20;
    }

    printf("AmigaBridge v1.0 starting\n");
    ui_add_log("Starting AmigaBridge...");

    /* Initialize IPC */
    if (ipc_init() != 0) {
        printf("  IPC: FAILED\n");
        ui_add_log("ERR: Cannot create IPC port");
        Delay(100);
        CloseWindow(win);
        close_libs();
        return 20;
    }
    printf("  IPC: OK (port '%s')\n", BRIDGE_PORT_NAME);
    ui_add_log("IPC port created");

    /* Open serial device - use high baud for FS-UAE PTY */
    if (serial_open(115200) != 0) {
        printf("  Serial: FAILED\n");
        ui_add_log("ERR: Cannot open serial");
        g_serial_connected = FALSE;
    } else {
        printf("  Serial: OK\n");
        g_serial_connected = TRUE;
        ui_add_log("Serial opened");
        /* Start first async read */
        serial_start_read();
    }

    /* Crash handler NOT installed at startup - enable via CRASHINIT command */

    /* Init optional modules */
    proc_init();
    font_init();
    chiplog_init();
    pool_init();
    clip_init();
    arexx_init();

    /* Set up periodic timer (200ms) for snoop drain and heartbeat */
    timerPort = CreateMsgPort();
    if (timerPort) {
        timerReq = (struct timerequest *)CreateIORequest(timerPort,
                    sizeof(struct timerequest));
        if (timerReq) {
            if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_VBLANK,
                           (struct IORequest *)timerReq, 0) == 0) {
                timerOpen = TRUE;
                timerSig = 1UL << timerPort->mp_SigBit;
                /* Fire first timer */
                timerReq->tr_node.io_Command = TR_ADDREQUEST;
                timerReq->tr_time.tv_secs = 0;
                timerReq->tr_time.tv_micro = 200000; /* 200ms */
                SendIO((struct IORequest *)timerReq);
            }
        }
    }

    /* Send READY to host */
    if (g_serial_connected) {
        protocol_send_raw("READY|1.0");
    }

    /* Initial UI draw */
    ui_redraw();

    /* Build signal mask */
    serialSig = serial_get_signal();
    ipcSig = ipc_get_signal();
    winSig = 1UL << win->UserPort->mp_SigBit;

    /* Main loop */
    while (running) {
        ULONG arexxSig = arexx_get_signal();
        signals = serialSig | ipcSig | winSig | timerSig | arexxSig | SIGBREAKF_CTRL_C;

        received = Wait(signals);

        /* Check CTRL-C */
        if (received & SIGBREAKF_CTRL_C) {
            ui_add_log("CTRL-C received");
            running = FALSE;
            break;
        }

        /* Check window events */
        if (received & winSig) {
            struct IntuiMessage *imsg;
            while ((imsg = (struct IntuiMessage *)GetMsg(win->UserPort)) != NULL) {
                ULONG iclass = imsg->Class;
                ReplyMsg((struct Message *)imsg);

                if (iclass == IDCMP_CLOSEWINDOW) {
                    ui_add_log("Window close");
                    running = FALSE;
                } else if (iclass == IDCMP_REFRESHWINDOW) {
                    BeginRefresh(win);
                    EndRefresh(win, TRUE);
                    g_ui_dirty = TRUE;
                }
            }
        }

        /* Check serial data */
        if (g_serial_connected && (received & serialSig)) {
            char ch;
            while (serial_check_read(&ch)) {
                handle_serial_byte(ch);
                /* Restart async read for next byte */
                serial_start_read();
            }
        }

        /* Check IPC messages */
        if (received & ipcSig) {
            ipc_process();
        }

        /* Check ARexx reply */
        if (arexxSig && (received & arexxSig)) {
            arexx_poll();
        }

        /* Timer tick — periodic 200ms wake-up */
        if (timerOpen && (received & timerSig)) {
            /* Collect the timer message */
            GetMsg(timerPort);

            /* Drain snoop ring buffer */
            if (g_serial_connected && snoop_is_active()) {
                snoop_drain();
            }

            /* Poll chip logger for changes */
            if (g_serial_connected) {
                chiplog_poll();
            }

            /* Poll ARexx for timeout */
            arexx_poll();

            /* Poll tail file streaming */
            if (g_serial_connected) {
                tail_poll();
            }

            /* Heartbeat (every ~5 seconds = 25 timer ticks) */
            hb_counter++;
            if (hb_counter >= 25 && g_serial_connected) {
                hb_counter = 0;
                send_heartbeat();
            }

            /* Re-arm timer */
            timerReq->tr_node.io_Command = TR_ADDREQUEST;
            timerReq->tr_time.tv_secs = 0;
            timerReq->tr_time.tv_micro = 200000;
            SendIO((struct IORequest *)timerReq);
        }

        /* Redraw UI if dirty */
        if (g_ui_dirty) {
            ui_redraw();
        }
    }

    /* Shutdown */
    ui_add_log("Shutting down...");
    ui_redraw();

    /* Notify connected clients */
    {
        int ci;
        for (ci = 0; ci < AB_MAX_CLIENTS; ci++) {
            struct ClientEntry *ce = client_find((ULONG)(ci + 1));
            if (ce && ce->replyPort) {
                ipc_send_to_client(ce->replyPort, ABMSG_SHUTDOWN,
                                   ce->clientId, 0, NULL, 0);
            }
        }
    }

    /* Clean up in reverse order */
    arexx_cleanup();
    clip_cleanup();
    pool_cleanup();
    proc_cleanup();
    chiplog_cleanup();
    font_cleanup();
    input_cleanup();
    snoop_stop();
    crash_cleanup();

    /* Clean up timer */
    if (timerOpen) {
        AbortIO((struct IORequest *)timerReq);
        WaitIO((struct IORequest *)timerReq);
        CloseDevice((struct IORequest *)timerReq);
    }
    if (timerReq) DeleteIORequest((struct IORequest *)timerReq);
    if (timerPort) DeleteMsgPort(timerPort);

    serial_close();
    ipc_cleanup();

    if (win) {
        CloseWindow(win);
        win = NULL;
    }

    close_libs();

    return 0;
}
