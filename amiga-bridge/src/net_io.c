/*
 * net_io.c - TCP/IP transport for AmigaBridge daemon via bsdsocket.library
 *            (RoadShow / AmiTCP / Miami / emulator bsdsocket emulation).
 *
 * The Amiga is the TCP SERVER: it listens; the host amiga-devbench connects in.
 * Socket readiness is delivered to the main Wait() loop via a SIGIO signal from
 * the stack. All sockets are non-blocking; reads are drained into a buffer and
 * handed up one byte at a time so the existing line assembler is reused.
 */
#include <exec/types.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/socket.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/filio.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <string.h>

#include "bridge_internal.h"

struct Library *SocketBase = NULL;

static LONG  listen_sock = -1;
static LONG  client_sock = -1;
static BYTE  io_sigbit   = -1;
static ULONG io_sigmask  = 0;

static char  rx_buf[512];
static int   rx_len = 0;
static int   rx_pos = 0;

static void set_nonblocking(LONG s)
{
    LONG one = 1;
    IoctlSocket(s, FIONBIO, (char *)&one);
}

/* Enable async (SIGIO) delivery for this socket so the stack signals our task
 * the instant the socket is readable. Without this, setting SBTC_SIGIOMASK
 * alone does NOT generate signals, and the daemon only services the socket on
 * its periodic timer tick (~200ms latency per request). */
static void set_async(LONG s)
{
    LONG one = 1;
    IoctlSocket(s, FIOASYNC, (char *)&one);
}

static void drop_client(void)
{
    if (client_sock >= 0) { CloseSocket(client_sock); client_sock = -1; }
    rx_len = rx_pos = 0;
    ui_add_log("TCP: client disconnected");
}

static void try_accept(void)
{
    struct sockaddr_in ca;
    LONG calen = sizeof(ca);
    LONG s = accept(listen_sock, (struct sockaddr *)&ca, &calen);
    if (s >= 0) {
        if (client_sock >= 0) CloseSocket(client_sock);   /* drop stale peer */
        client_sock = s;
        set_nonblocking(client_sock);
        set_async(client_sock);    /* wake the daemon immediately on incoming data */
        {
            /* Throughput tuning for big streamed responses (screenshots):
             * disable Nagle so each row isn't held waiting for an ACK, and
             * enlarge the send buffer so more queues before back-pressure.
             * Both are best-effort - ignore failures on stacks that lack them. */
            LONG one = 1;
            LONG sndbuf = 128 * 1024;
            setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof(one));
            setsockopt(client_sock, SOL_SOCKET, SO_SNDBUF, (char *)&sndbuf, sizeof(sndbuf));
        }
        rx_len = rx_pos = 0;
        ui_add_log("TCP: client connected");
    }
}

int net_open(ULONG port)
{
    struct sockaddr_in sa;
    LONG one = 1;

    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (!SocketBase) {
        ui_add_log("ERR: no bsdsocket.library (TCP/IP stack not running?)");
        return -1;
    }

    /* Ask the stack to signal us on socket I/O readiness */
    io_sigbit = AllocSignal(-1);
    if (io_sigbit == -1) {
        ui_add_log("ERR: AllocSignal failed");
        CloseLibrary(SocketBase);
        SocketBase = NULL;
        return -1;
    }
    io_sigmask = 1UL << io_sigbit;
    SocketBaseTags(SBTM_SETVAL(SBTC_SIGIOMASK),  (ULONG)io_sigmask,
                   SBTM_SETVAL(SBTC_SIGURGMASK), (ULONG)io_sigmask,
                   TAG_END);

    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        ui_add_log("ERR: socket() failed");
        net_close();
        return -1;
    }
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one));

    memset(&sa, 0, sizeof(sa));
    sa.sin_family      = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port        = htons((UWORD)port);

    if (bind(listen_sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        ui_add_log("ERR: bind() failed");
        net_close();
        return -1;
    }
    if (listen(listen_sock, 1) < 0) {
        ui_add_log("ERR: listen() failed");
        net_close();
        return -1;
    }
    set_nonblocking(listen_sock);
    set_async(listen_sock);     /* wake immediately on incoming connections */

    ui_add_log("TCP: listening");
    return 0;
}

void net_close(void)
{
    if (client_sock >= 0) { CloseSocket(client_sock); client_sock = -1; }
    if (listen_sock >= 0) { CloseSocket(listen_sock); listen_sock = -1; }
    if (SocketBase) {
        /* Stop the stack signalling us before we free the signal bit */
        SocketBaseTags(SBTM_SETVAL(SBTC_SIGIOMASK),  0UL,
                       SBTM_SETVAL(SBTC_SIGURGMASK), 0UL,
                       TAG_END);
        CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
    if (io_sigbit != -1) { FreeSignal(io_sigbit); io_sigbit = -1; io_sigmask = 0; }
    rx_len = rx_pos = 0;
}

ULONG net_get_signal(void)
{
    return io_sigmask;
}

BOOL net_is_open(void)
{
    return (client_sock >= 0) ? TRUE : FALSE;
}

int net_check_read(char *out_byte)
{
    LONG n;

    if (!SocketBase) return 0;

    /* Serve buffered bytes first */
    if (rx_pos < rx_len) {
        *out_byte = rx_buf[rx_pos++];
        return 1;
    }

    /* No peer yet: try to accept one */
    if (client_sock < 0) {
        if (listen_sock >= 0) try_accept();
        if (client_sock < 0) return 0;
    }

    /* Refill from the client socket (non-blocking) */
    n = recv(client_sock, rx_buf, sizeof(rx_buf), 0);
    if (n > 0) {
        rx_len = (int)n;
        rx_pos = 0;
        *out_byte = rx_buf[rx_pos++];
        return 1;
    } else if (n == 0) {
        drop_client();               /* peer closed */
        return 0;
    } else {
        if (Errno() != EWOULDBLOCK && Errno() != EINTR) drop_client();
        return 0;
    }
}

int net_write(const char *buf, int len)
{
    int sent = 0;

    if (client_sock < 0) return -1;

    while (sent < len) {
        LONG n = send(client_sock, (APTR)(buf + sent), len - sent, 0);
        if (n > 0) {
            sent += (int)n;
        } else if (n < 0 && Errno() == EINTR) {
            continue;                /* interrupted: retry, no penalty */
        } else if (n < 0 && Errno() == EWOULDBLOCK) {
            /* Send buffer full. Block until the socket is writable again
             * instead of polling with Delay(): WaitSelect lets the stack wake
             * us the instant space frees up, so a multi-MB true-colour
             * screenshot streams at link speed rather than being throttled to
             * Delay() granularity (~20ms/burst). A generous timeout still
             * guards against a host that has really gone away (drop the peer
             * only on a long sustained stall, never on normal back-pressure). */
            fd_set wfds;
            struct timeval tv;
            LONG r;

            FD_ZERO(&wfds);
            FD_SET(client_sock, &wfds);
            tv.tv_sec  = 30;
            tv.tv_usec = 0;
            r = WaitSelect(client_sock + 1, NULL, &wfds, NULL, &tv, NULL);
            if (r <= 0) {            /* timeout (peer gone) or select error */
                drop_client();
                return sent;
            }
        } else {
            drop_client();
            return sent;
        }
    }
    return sent;
}
