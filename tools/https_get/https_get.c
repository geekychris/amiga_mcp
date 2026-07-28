/*
 * https_get — minimal HTTPS GET client for AmigaOS 4 (PPC).
 *
 * Works around Python-on-OS4's _ssl/_socket fd-interop bug (task #94)
 * by keeping the whole HTTPS exchange in a single C binary that both
 * uses newlib's POSIX socket() AND links libamisslauto — the auto-
 * initializer exports its ISocket / SocketBase as weak globals so
 * newlib's socket wrapper AND AmiSSL end up sharing the same base.
 *
 * Python calls it via `os.system("DH1:https_get host / >T:body")`.
 *
 * Usage:
 *   https_get <host>[:<port>] <path> [-servername <sni>] [-insecure]
 *
 * Response body → stdout; process exits 0 on success. Errors → stdout
 * (not stderr, so the devbench bridge's stdout-only capture sees them)
 * + non-zero exit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

static void out(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
}

static void usage(const char *argv0) {
    out("usage: %s <host>[:<port>] <path> [-servername <sni>] [-insecure]\n"
        "  default port 443, default sni=host\n", argv0);
}

int main(int argc, char **argv) {
    if (argc < 3) { usage(argv[0]); return 1; }

    char host[256], sni[256];
    int port = 443;
    int verify = 1;
    strncpy(host, argv[1], sizeof(host)-1); host[sizeof(host)-1] = 0;
    char *colon = strchr(host, ':');
    if (colon) { *colon = 0; port = atoi(colon + 1); if (port <= 0) port = 443; }
    strncpy(sni, host, sizeof(sni)-1); sni[sizeof(sni)-1] = 0;
    const char *path = argv[2];
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "-servername") && i+1 < argc) {
            strncpy(sni, argv[++i], sizeof(sni)-1); sni[sizeof(sni)-1] = 0;
        } else if (!strcmp(argv[i], "-insecure")) {
            verify = 0;
        } else if (argv[i][0] == '>' || argv[i][0] == '<' ||
                   (argv[i][0] == '2' && argv[i][1] == '>')) {
            /* Swallow shell redirection tokens some AmigaDOS shells
             * pass through as argv. */
            continue;
        } else {
            out("unknown arg: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    out("[https_get] start\n");

    /* Resolve host: raw IPv4 or DNS via newlib+bsdsocket. */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        struct hostent *he = gethostbyname(host);
        if (!he || !he->h_addr) {
            out("gethostbyname(%s) failed (errno=%d)\n", host, errno);
            return 2;
        }
        memcpy(&addr.sin_addr, he->h_addr, sizeof(addr.sin_addr));
    }
    out("[https_get] resolved\n");

    /* TCP connect. */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { out("socket() failed (errno=%d)\n", errno); return 3; }
    out("[https_get] socket=%d\n", sock);
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        out("connect() failed (errno=%d)\n", errno);
        close(sock); return 4;
    }
    out("[https_get] connected\n");

    /* SSL init. libamisslauto's ctor has already opened amissl.library. */
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) { out("SSL_CTX_new failed\n"); close(sock); return 5; }
    if (verify) {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
        SSL_CTX_set_default_verify_paths(ctx);
        SSL_CTX_load_verify_locations(ctx, NULL, "AmiSSL:Certs");
    } else {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    }

    SSL *ssl = SSL_new(ctx);
    if (!ssl) { out("SSL_new failed\n"); SSL_CTX_free(ctx); close(sock); return 5; }
    SSL_set_tlsext_host_name(ssl, sni);
    if (SSL_set_fd(ssl, sock) != 1) {
        out("SSL_set_fd failed\n");
        SSL_free(ssl); SSL_CTX_free(ctx); close(sock); return 6;
    }
    int rc = SSL_connect(ssl);
    if (rc <= 0) {
        int e = SSL_get_error(ssl, rc);
        out("SSL_connect rc=%d err=%d errno=%d\n", rc, e, errno);
        unsigned long qerr;
        while ((qerr = ERR_get_error()) != 0) {
            char ebuf[256];
            ERR_error_string_n(qerr, ebuf, sizeof(ebuf));
            out("  err: %s\n", ebuf);
        }
        SSL_free(ssl); SSL_CTX_free(ctx); close(sock); return 6;
    }
    out("[https_get] TLS %s / %s\n", SSL_get_version(ssl), SSL_get_cipher_name(ssl));

    /* Send GET. */
    char req[1024];
    int rl = snprintf(req, sizeof(req),
        "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: os4/https_get\r\n"
        "Accept: */*\r\nConnection: close\r\n\r\n",
        path, sni);
    if (SSL_write(ssl, req, rl) != rl) {
        out("SSL_write short\n");
        SSL_shutdown(ssl); SSL_free(ssl); SSL_CTX_free(ctx); close(sock);
        return 7;
    }

    /* Drain response. */
    char buf[4096];
    int n;
    while ((n = SSL_read(ssl, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, n, stdout);
    }
    fflush(stdout);

    SSL_shutdown(ssl); SSL_free(ssl); SSL_CTX_free(ctx); close(sock);
    return 0;
}
