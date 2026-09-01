// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins

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
#include <stdlib.h>

#include "bridge_internal.h"

#ifndef __PPC__
extern struct ExecBase *SysBase;
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;
#endif

/* Layout metrics derived at runtime from the inherited screen font. */
static int g_line_h   = 12;   /* per-line cell height (font height + leading) */
static int g_baseline = 8;    /* font baseline offset within the cell */
static int g_top      = 2;    /* top margin inside the GZZ area */

/* Last protocol command received from the host (for the status window). */
char g_last_cmd[80] = "";

/* ─── Wedge diagnostics ──────────────────────────────────────────────────
 *
 * The main loop sets a phase name each time it enters a distinct section.
 * A counter increments so we can tell "loop is progressing" vs "loop is
 * frozen inside the current phase".
 *
 * Also mirrored to SYS:BridgeLogs/live.log (truncate+write each tick) so
 * we can `ssh chris@amiga.local cat` it after a wedge — no bridge protocol
 * required. The file open/close per tick is intentional: on any freeze
 * the last successful Close is on disk and readable.
 */
static char       g_phase[32]     = "boot";
static ULONG      g_phase_counter = 0;

static void phase_set(const char *name)
{
    int i;
    for (i = 0; name[i] && i < (int)sizeof(g_phase) - 1; i++) g_phase[i] = name[i];
    g_phase[i] = '\0';
    g_phase_counter++;
}

/* Refresh the on-screen phase display every timer tick so a live viewer
 * (or a screenshot at wedge time) sees the current phase + counter. We
 * do NOT do file I/O here — Amiberry's DOS emulation wedges on the
 * repeated open/write/close pattern.  */
static void phase_log_tick(void)
{
    g_ui_dirty = TRUE;
}

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
#define WIN_WIDTH  330
#define WIN_HEIGHT 210   /* room for Cmd + Phase lines below the log */
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

/* Lines shown: 3 status (Serial/Host/Clients) + log lines + 1 (Msgs). */
#define UI_NUM_LINES (3 + UI_MAX_LOG_LINES + 2)  /* status + logs + Msgs + Cmd */

static struct Window *open_window(void)
{
    struct NewWindow nw;
    struct Window *w;
    int innerW, innerH, totalW, totalH;

    memset(&nw, 0, sizeof(nw));
    nw.LeftEdge = WIN_LEFT;
    nw.TopEdge = WIN_TOP;
    nw.Width = WIN_WIDTH;
    nw.Height = 120;                 /* provisional - resized to fit the font */
    nw.DetailPen = 0;
    nw.BlockPen = 1;
    nw.Title = (UBYTE *)BRIDGE_VERSION_STR;
    nw.Flags = WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
               WFLG_ACTIVATE | WFLG_SMART_REFRESH |
               WFLG_GIMMEZEROZERO;
    nw.IDCMPFlags = IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW;
    nw.Type = WBENCHSCREEN;
    nw.MinWidth = 80;   nw.MinHeight = 40;
    nw.MaxWidth = 2000; nw.MaxHeight = 2000;

    w = OpenWindow(&nw);
    if (!w) return NULL;

    /* Inherit the screen font and derive the whole layout from its real
     * metrics, then size the window to fit exactly - no hardcoded font/size. */
    g_line_h   = w->RPort->TxHeight + 2;     /* font height + leading */
    g_baseline = w->RPort->TxBaseline;
    g_top      = 2;

    /* Width: fit the wider of the status line and a representative command
     * line (command + params), so the "Cmd:" line shows its arguments. */
    {
        int wStatus = (int)TextLength(w->RPort,
                          (CONST_STRPTR)"Serial: Disconnected   TX:000000", 32);
        int wCmd    = (int)TextLength(w->RPort,
                          (CONST_STRPTR)"Cmd: WRITEFILE|MEDIA1:_mcp/longfilename.bin|0|abcd", 50);
        innerW = (wStatus > wCmd ? wStatus : wCmd) + TEXT_LEFT + 6;
    }
    innerH = g_top + UI_NUM_LINES * g_line_h + 2;
    totalW = innerW + w->BorderLeft + w->BorderRight;
    totalH = innerH + w->BorderTop + w->BorderBottom;
    ChangeWindowBox(w, w->LeftEdge, w->TopEdge, totalW, totalH);
    return w;
}

static void draw_text_line(struct RastPort *rp, int lineNum,
                           const char *text)
{
    int y = g_top + lineNum * g_line_h;          /* cell top (GZZ coords) */
    int availW = (int)win->GZZWidth - TEXT_LEFT - 2;
    int len = (int)strlen(text);
    struct TextExtent te;
    int fit;

    /* Clear the whole line cell so nothing leaks under or between lines. */
    SetAPen(rp, 0);
    RectFill(rp, 0, y, win->GZZWidth - 1, y + g_line_h - 1);

    if (availW < 1 || len == 0) return;

    /* Truncate to what fits the current width/font (handles proportional). */
    fit = (int)TextFit(rp, (CONST_STRPTR)text, (ULONG)len, &te, NULL, 1,
                       (UWORD)availW, (UWORD)g_line_h);

    SetAPen(rp, 1);
    Move(rp, TEXT_LEFT, y + g_baseline);
    Text(rp, (CONST_STRPTR)text, fit);
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

    /* Message counters */
    sprintf(linebuf, "Msgs: TX:%lu RX:%lu",
            (unsigned long)g_tx_count,
            (unsigned long)g_rx_count);
    draw_text_line(rp, 3 + UI_MAX_LOG_LINES, linebuf);

    /* Last command received from the host (every command, not just RunAsync) */
    {
        static char cmdline[96];
        sprintf(cmdline, "Cmd: %s", g_last_cmd[0] ? g_last_cmd : "-");
        draw_text_line(rp, 3 + UI_MAX_LOG_LINES + 1, cmdline);
    }

    /* Phase + counter — the counter proves the main loop is progressing.
     * If a screenshot shows the same counter across two grabs, we're
     * wedged and `phase` names the section we stalled in. */
    {
        static char phaseline[96];
        sprintf(phaseline, "Phase: %s (%lu)",
                g_phase, (unsigned long)g_phase_counter);
        draw_text_line(rp, 3 + UI_MAX_LOG_LINES + 2, phaseline);
    }

    g_ui_dirty = FALSE;
}

void ui_add_log(const char *msg)
{
    strncpy(g_ui_logs[g_ui_log_head], msg, UI_MAX_LOG_LEN - 1);
    g_ui_logs[g_ui_log_head][UI_MAX_LOG_LEN - 1] = '\0';
    g_ui_log_head = (g_ui_log_head + 1) % UI_MAX_LOG_LINES;
    g_ui_dirty = TRUE;
}

/* Record the last protocol command for the status window (all commands). */
void ui_set_last_cmd(const char *cmd)
{
    if (!cmd) return;
    strncpy(g_last_cmd, cmd, sizeof(g_last_cmd) - 1);
    g_last_cmd[sizeof(g_last_cmd) - 1] = '\0';
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

    /* Transport selection from CLI args.
     *
     * On the classic m68k build serial is the historical default; TCP was
     * opt-in via the `TCP` arg.  On the PPC/OS4 build we flip the default
     * because TCP-over-bsdsocket is the whole point of the pivot: much
     * higher throughput + no serial-timing quirks.  Users can still force
     * serial with the explicit `SERIAL` arg (or `SERIAL <baud>`).
     *
     *   amiga-bridge                -> PPC: TCP :2345    m68k: serial 115200
     *   amiga-bridge TCP            -> TCP :2345
     *   amiga-bridge TCP <port>     -> TCP :<port>
     *   amiga-bridge SERIAL         -> serial 115200
     *   amiga-bridge SERIAL <baud>  -> serial <baud>
     */
#ifdef __PPC__
    int   sel_mode  = TRANSPORT_TCP;
    ULONG sel_param = 2345;
#else
    int   sel_mode  = TRANSPORT_SERIAL;
    ULONG sel_param = 115200;
#endif
    if (argc >= 2 && (strcmp(argv[1], "TCP") == 0 || strcmp(argv[1], "tcp") == 0)) {
        ULONG tcp_port = 2345;
        if (argc >= 3) {
            long p = atol(argv[2]);
            if (p > 0 && p < 65536) tcp_port = (ULONG)p;
        }
        sel_mode  = TRANSPORT_TCP;
        sel_param = tcp_port;
    } else if (argc >= 2 && (strcmp(argv[1], "SERIAL") == 0 || strcmp(argv[1], "serial") == 0)) {
        ULONG baud = 115200;
        if (argc >= 3) {
            long b = atol(argv[2]);
            if (b > 0) baud = (ULONG)b;
        }
        sel_mode  = TRANSPORT_SERIAL;
        sel_param = baud;
    }

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

    printf(BRIDGE_VERSION_STR " (build %s) starting\n", g_bridge_build);
    ui_add_log("Starting " BRIDGE_VERSION_STR);
    {
        static char bld[80];
        sprintf(bld, "Build: %s", g_bridge_build);
        ui_add_log(bld);
    }

    /* If a previous daemon is already running, ask it to quit and take over,
     * so simply re-running the binary cleanly replaces the old instance
     * (enables remote restart without manual intervention). */
    {
        struct Task *oldtask = NULL;
        struct MsgPort *p;
        int tries;
        Forbid();
        p = FindPort((CONST_STRPTR)BRIDGE_PORT_NAME);
        if (p) oldtask = p->mp_SigTask;
        Permit();
        if (oldtask) {
            ui_add_log("Replacing running AmigaBridge instance...");
            Signal(oldtask, SIGBREAKF_CTRL_C);
            for (tries = 0; tries < 30; tries++) {   /* up to ~6s */
                Forbid();
                p = FindPort((CONST_STRPTR)BRIDGE_PORT_NAME);
                Permit();
                if (!p) break;
                Delay(10);   /* 0.2s */
            }
        }
    }

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

    /* Open the selected transport (serial.device or TCP/bsdsocket) */
    if (transport_open(sel_mode, sel_param) != 0) {
        printf("  Transport: FAILED\n");
        ui_add_log("ERR: Cannot open transport");
        g_serial_connected = FALSE;
    } else {
        printf("  Transport: OK\n");
        /* Enter the loop disconnected; the rising-edge block in the main loop
         * greets the peer (serial: fires iteration 1; tcp: fires on accept). */
        g_serial_connected = FALSE;
        transport_start_read();

        /* Reflect transport mode + param in the window title so at a glance
         * you can tell what the running bridge is doing. serial_open /
         * net_open have already ui_add_log'd the specifics (unit/baud or
         * per-interface IPs). */
        {
            static char title[96];
            if (sel_mode == TRANSPORT_TCP) {
                snprintf(title, sizeof(title),
                         "%s  -  TCP :%lu (bsdsocket)",
                         BRIDGE_VERSION_STR, (unsigned long)sel_param);
            } else {
                snprintf(title, sizeof(title),
                         "%s  -  Serial unit 0 @ %lu baud",
                         BRIDGE_VERSION_STR, (unsigned long)sel_param);
            }
            SetWindowTitles(win, (STRPTR)title, (STRPTR)~0);
        }
    }

    /* Crash handler NOT installed at startup - enable via CRASHINIT command */

    /* Init optional modules */
    proc_init();
    font_init();
    chiplog_init();
    pool_init();
    clip_init();
    arexx_init();
    sys_init_uptime();

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

    /* Initial UI draw */
    ui_redraw();

    /* Build signal mask */
    serialSig = transport_get_signal();   /* serial port sig OR tcp SIGIO */
    ipcSig = ipc_get_signal();
    winSig = 1UL << win->UserPort->mp_SigBit;

    /* Main loop */
    while (running) {
        ULONG arexxSig = arexx_get_signal();
        signals = serialSig | ipcSig | winSig | timerSig | arexxSig | SIGBREAKF_CTRL_C;

        phase_set("wait");
        received = Wait(signals);

        /* Check CTRL-C */
        if (received & SIGBREAKF_CTRL_C) {
            phase_set("ctrl_c");
            ui_add_log("CTRL-C received");
            running = FALSE;
            break;
        }

        /* Check shutdown request from protocol handler */
        if (g_shutdown_requested) {
            phase_set("shutdown");
            ui_add_log("Shutdown requested");
            running = FALSE;
            break;
        }

        /* Check window events */
        if (received & winSig) {
            struct IntuiMessage *imsg;
            phase_set("window");
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

        /* Drain the transport (serial bytes, or TCP accept + recv).
         * Polled unconditionally each loop iteration — the loop wakes at
         * least every 200ms on the timer tick, so TCP accept/recv works even
         * when the bsdsocket stack's SIGIO does not fire. transport_check_read
         * is non-blocking and returns 0 when there is nothing pending. */
        {
            char ch;
            phase_set("transport");
            while (transport_check_read(&ch)) {
                phase_set("transport_dispatch");
                handle_serial_byte(ch);
                transport_start_read();
            }
        }

        /* Maintain link-connected state; greet a freshly connected peer */
        {
            BOOL now_conn = transport_is_open();
            if (now_conn && !g_serial_connected) {
                g_serial_connected = TRUE;
                protocol_send_raw("READY|1.0");
            } else if (!now_conn && g_serial_connected) {
                g_serial_connected = FALSE;
            }
        }

        /* Check IPC messages */
        if (received & ipcSig) {
            phase_set("ipc");
            ipc_process();
        }

        /* Poll debugger for TRAP hits — check on EVERY iteration,
         * not just timer ticks, so we catch the first BP hit before
         * the target runs past additional breakpoints. */
        if (g_serial_connected) {
            phase_set("dbg_poll");
            dbg_poll();
        }

        /* Check ARexx reply */
        if (arexxSig && (received & arexxSig)) {
            phase_set("arexx");
            arexx_poll();
        }

        /* Timer tick — periodic 200ms wake-up */
        if (timerOpen && (received & timerSig)) {
            phase_set("timer");
            /* Collect the timer message */
            GetMsg(timerPort);

            /* Drain snoop ring buffer */
            if (g_serial_connected && snoop_is_active()) {
                phase_set("snoop_drain");
                snoop_drain();
            }

            /* Poll chip logger for changes */
            if (g_serial_connected) {
                phase_set("chiplog");
                chiplog_poll();
            }

            /* Poll ARexx for timeout */
            phase_set("arexx_poll");
            arexx_poll();

            /* Poll tail file streaming */
            if (g_serial_connected) {
                phase_set("tail_poll");
                tail_poll();
            }

            /* Heartbeat (every ~5 seconds = 25 timer ticks) */
            hb_counter++;
            if (hb_counter >= 25 && g_serial_connected) {
                hb_counter = 0;
                phase_set("heartbeat");
                send_heartbeat();
            }

            /* Flush the wedge-diagnostics log once per timer tick. */
            phase_set("phase_log");
            phase_log_tick();

            /* Re-arm timer */
            phase_set("timer_rearm");
            timerReq->tr_node.io_Command = TR_ADDREQUEST;
            timerReq->tr_time.tv_secs = 0;
            timerReq->tr_time.tv_micro = 200000;
            SendIO((struct IORequest *)timerReq);
        }

        /* Redraw UI if dirty */
        if (g_ui_dirty) {
            phase_set("redraw");
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
    dbg_cleanup();
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

    transport_close();
    ipc_cleanup();

    if (win) {
        CloseWindow(win);
        win = NULL;
    }

    close_libs();

    return 0;
}
