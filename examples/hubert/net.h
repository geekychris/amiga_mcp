/*
 * net.h — tiny bsdsocket.library wrapper.
 *
 * Opens the library on demand, resolves a hostname, opens a TCP connection,
 * and provides blocking send/recv helpers with a hard timeout so a slow
 * peer can't hang the whole shell. Intended for one-shot HTTP requests, not
 * for keeping long-lived sockets around — reopen per request.
 *
 * Under RoadShow / AmiTCP / Amiberry's bsdsocket_emu the semantics of
 * these calls are close enough to POSIX that we can keep this file
 * portable-looking. The tests stub the whole thing out by using
 * NET_STUB_MODE (see net.c).
 */
#ifndef HUBERT_NET_H
#define HUBERT_NET_H

typedef enum {
    NET_OK = 0,
    NET_ERR_NO_LIB,        /* bsdsocket.library not present */
    NET_ERR_RESOLVE,
    NET_ERR_CONNECT,
    NET_ERR_SEND,
    NET_ERR_RECV,
    NET_ERR_TIMEOUT,
    NET_ERR_CLOSED,        /* peer FIN */
    NET_ERR_STUB_EOS,      /* stub ran out of scripted data */
} NetResult;

typedef struct NetConn NetConn;

int  net_startup(void);         /* opens bsdsocket.library; safe to call twice */
void net_shutdown(void);

/* Open a TCP connection. `timeout_ms` guards DNS + connect. On success
 * returns a non-NULL handle; on failure returns NULL and *err carries a
 * NET_ERR_ value. */
NetConn *net_connect(const char *host, int port, int timeout_ms, NetResult *err);

/* Send exactly len bytes. Blocks up to timeout_ms. */
NetResult net_send_all(NetConn *c, const char *buf, int len, int timeout_ms);

/* Read up to bufSize bytes into buf. On success returns NET_OK and writes
 * the received count to *out_len. NET_ERR_CLOSED means clean EOF. */
NetResult net_recv(NetConn *c, char *buf, int bufSize, int *out_len, int timeout_ms);

/* Close and free. Safe with NULL. */
void net_close(NetConn *c);

/* ─── Test-mode stubbing (host build only) ─────────────────────────────
 *
 * When HAVE_AMIGA_DOS is undefined (host build), the functions above are
 * backed by a scriptable stub instead of bsdsocket. Tests set up:
 *   net_stub_reset();
 *   net_stub_expect_send("POST /api/chat...");
 *   net_stub_queue_recv("HTTP/1.1 200 OK\r\n...");
 *   net_stub_queue_recv_close();
 * ...run the code under test... then assert on:
 *   net_stub_last_host / net_stub_last_port
 *   net_stub_send_captured() — the concatenated bytes we sent.
 */
void        net_stub_reset(void);
void        net_stub_queue_recv(const char *bytes);
void        net_stub_queue_recv_close(void);   /* schedule a clean EOF */
const char *net_stub_send_captured(int *out_len);
const char *net_stub_last_host(void);
int         net_stub_last_port(void);

#endif
