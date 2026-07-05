/* net.c — Amiga bsdsocket implementation + host-side stub. */

#include "net.h"

#include <string.h>
#include <stdlib.h>

#ifdef HAVE_AMIGA_DOS

/* ─── Amiga implementation ────────────────────────────────────────────── */

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/socket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/filio.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>

/* SocketBase is declared extern in <proto/socket.h> above — the linker
 * fills it in from libamiga (or wherever the bsdsocket glue lives). We
 * only need to Open the library and check the pointer. */
struct Library *SocketBase = 0;

struct NetConn {
    long sock;   /* LONG under bsdsocket */
};

int net_startup(void)
{
    if (SocketBase) return 0;
    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    return SocketBase ? 0 : -1;
}

void net_shutdown(void)
{
    if (SocketBase) { CloseLibrary(SocketBase); SocketBase = 0; }
}

/* Wait up to timeout_ms on a socket for read/write readiness. Returns:
 *   >0  socket ready
 *    0  timeout
 *   -1  error */
static int wait_ready(long s, int for_write, int timeout_ms)
{
    fd_set fds;
    struct timeval tv;
    long r;
    FD_ZERO(&fds);
    FD_SET(s, &fds);
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (for_write) r = WaitSelect(s + 1, 0, &fds, 0, &tv, 0);
    else            r = WaitSelect(s + 1, &fds, 0, 0, &tv, 0);
    return (int)r;
}

NetConn *net_connect(const char *host, int port, int timeout_ms, NetResult *err)
{
    struct hostent *he;
    struct sockaddr_in sa;
    long s;
    long one = 1;
    NetConn *c;
    int ready;

    if (!SocketBase && net_startup() != 0) { if (err) *err = NET_ERR_NO_LIB; return 0; }

    he = gethostbyname((STRPTR)host);
    if (!he || !he->h_addr_list || !he->h_addr_list[0]) {
        if (err) *err = NET_ERR_RESOLVE;
        return 0;
    }

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        if (err) *err = NET_ERR_CONNECT;
        return 0;
    }

    /* Non-blocking so we can enforce our timeout. */
    IoctlSocket(s, FIONBIO, (char *)&one);

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons((unsigned short)port);
    memcpy(&sa.sin_addr, he->h_addr_list[0], he->h_length);

    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        int e = Errno();
        if (e != EINPROGRESS && e != EWOULDBLOCK) {
            CloseSocket(s);
            if (err) *err = NET_ERR_CONNECT;
            return 0;
        }
    }
    ready = wait_ready(s, 1, timeout_ms);
    if (ready <= 0) {
        CloseSocket(s);
        if (err) *err = (ready == 0) ? NET_ERR_TIMEOUT : NET_ERR_CONNECT;
        return 0;
    }

    c = (NetConn *)AllocVec(sizeof(NetConn), MEMF_ANY);
    if (!c) { CloseSocket(s); if (err) *err = NET_ERR_CONNECT; return 0; }
    c->sock = s;
    if (err) *err = NET_OK;
    return c;
}

NetResult net_send_all(NetConn *c, const char *buf, int len, int timeout_ms)
{
    int sent = 0;
    while (sent < len) {
        long n;
        int ready = wait_ready(c->sock, 1, timeout_ms);
        if (ready == 0) return NET_ERR_TIMEOUT;
        if (ready < 0)  return NET_ERR_SEND;
        n = send(c->sock, (APTR)(buf + sent), len - sent, 0);
        if (n > 0) { sent += (int)n; continue; }
        if (n < 0) {
            int e = Errno();
            if (e == EINTR || e == EWOULDBLOCK) continue;
            return NET_ERR_SEND;
        }
    }
    return NET_OK;
}

NetResult net_recv(NetConn *c, char *buf, int bufSize, int *out_len, int timeout_ms)
{
    long n;
    int ready = wait_ready(c->sock, 0, timeout_ms);
    if (ready == 0) return NET_ERR_TIMEOUT;
    if (ready < 0)  return NET_ERR_RECV;
    n = recv(c->sock, buf, bufSize, 0);
    if (n == 0)  return NET_ERR_CLOSED;
    if (n < 0) {
        int e = Errno();
        if (e == EINTR || e == EWOULDBLOCK) { *out_len = 0; return NET_OK; }
        return NET_ERR_RECV;
    }
    *out_len = (int)n;
    return NET_OK;
}

void net_close(NetConn *c)
{
    if (!c) return;
    if (c->sock >= 0) CloseSocket(c->sock);
    FreeVec(c);
}

/* Stub interface is a no-op on Amiga side — never called there. */
void        net_stub_reset(void) {}
void        net_stub_queue_recv(const char *b)     { (void)b; }
void        net_stub_queue_recv_close(void)         {}
const char *net_stub_send_captured(int *n)          { if (n) *n = 0; return ""; }
const char *net_stub_last_host(void)                { return ""; }
int         net_stub_last_port(void)                { return 0; }

#else

/* ─── Host stub — scripted send/recv for unit tests ───────────────────── */

#define STUB_BUF 65536
static char g_stub_send[STUB_BUF];
static int  g_stub_send_len;
static char g_stub_recv[STUB_BUF];
static int  g_stub_recv_len;
static int  g_stub_recv_pos;
static int  g_stub_recv_closed;
static char g_stub_host[128];
static int  g_stub_port;

struct NetConn { int fake; };
static NetConn g_stub_conn;

int  net_startup(void)  { return 0; }
void net_shutdown(void) {}

void net_stub_reset(void)
{
    g_stub_send_len = 0; g_stub_send[0] = '\0';
    g_stub_recv_len = 0; g_stub_recv_pos = 0; g_stub_recv_closed = 0;
    g_stub_host[0] = '\0'; g_stub_port = 0;
}

void net_stub_queue_recv(const char *bytes)
{
    int n = 0;
    while (bytes[n] && g_stub_recv_len < STUB_BUF - 1) {
        g_stub_recv[g_stub_recv_len++] = bytes[n++];
    }
}
void net_stub_queue_recv_close(void) { g_stub_recv_closed = 1; }
const char *net_stub_send_captured(int *out) { if (out) *out = g_stub_send_len; return g_stub_send; }
const char *net_stub_last_host(void) { return g_stub_host; }
int         net_stub_last_port(void) { return g_stub_port; }

NetConn *net_connect(const char *host, int port, int timeout_ms, NetResult *err)
{
    (void)timeout_ms;
    strncpy(g_stub_host, host, sizeof(g_stub_host) - 1);
    g_stub_host[sizeof(g_stub_host) - 1] = '\0';
    g_stub_port = port;
    if (err) *err = NET_OK;
    return &g_stub_conn;
}

NetResult net_send_all(NetConn *c, const char *buf, int len, int timeout_ms)
{
    (void)c; (void)timeout_ms;
    if (g_stub_send_len + len >= STUB_BUF) return NET_ERR_SEND;
    memcpy(g_stub_send + g_stub_send_len, buf, len);
    g_stub_send_len += len;
    g_stub_send[g_stub_send_len] = '\0';
    return NET_OK;
}

NetResult net_recv(NetConn *c, char *buf, int bufSize, int *out_len, int timeout_ms)
{
    int n;
    (void)c; (void)timeout_ms;
    if (g_stub_recv_pos >= g_stub_recv_len) {
        if (g_stub_recv_closed) return NET_ERR_CLOSED;
        return NET_ERR_STUB_EOS;
    }
    n = g_stub_recv_len - g_stub_recv_pos;
    if (n > bufSize) n = bufSize;
    memcpy(buf, g_stub_recv + g_stub_recv_pos, n);
    g_stub_recv_pos += n;
    *out_len = n;
    return NET_OK;
}

void net_close(NetConn *c) { (void)c; }

#endif
