/*
 * clipboard_bridge.c - Clipboard access via clipboard.device + iffparse.library
 *
 * Reads and writes text content from/to the system clipboard
 * using IFF FTXT/CHRS format.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <devices/clipboard.h>
#include <libraries/iffparse.h>
#include <proto/exec.h>
#include <proto/iffparse.h>

#include <string.h>
#include <stdio.h>

#include "bridge_internal.h"

extern struct ExecBase *SysBase;
static struct MsgPort *g_clipPort = NULL;
static struct IOClipReq *g_clipIO = NULL;
static BOOL g_device_open = FALSE;

/* IFF type/chunk IDs */
#define ID_FTXT MAKE_ID('F','T','X','T')
#define ID_CHRS MAKE_ID('C','H','R','S')

/*
 * Initialize clipboard access.
 * Opens iffparse.library and clipboard.device unit 0.
 */
void clip_init(void)
{
    IFFParseBase = OpenLibrary((CONST_STRPTR)"iffparse.library", 37);
    if (!IFFParseBase) {
        printf("Clipboard: WARNING - iffparse.library not available\n");
        return;
    }

    g_clipPort = CreateMsgPort();
    if (!g_clipPort) {
        printf("Clipboard: WARNING - could not create MsgPort\n");
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        return;
    }

    g_clipIO = (struct IOClipReq *)CreateIORequest(g_clipPort,
                                                    sizeof(struct IOClipReq));
    if (!g_clipIO) {
        printf("Clipboard: WARNING - could not create IORequest\n");
        DeleteMsgPort(g_clipPort);
        g_clipPort = NULL;
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        return;
    }

    if (OpenDevice((CONST_STRPTR)"clipboard.device", 0,
                    (struct IORequest *)g_clipIO, 0) != 0) {
        printf("Clipboard: WARNING - could not open clipboard.device\n");
        DeleteIORequest((struct IORequest *)g_clipIO);
        g_clipIO = NULL;
        DeleteMsgPort(g_clipPort);
        g_clipPort = NULL;
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
        return;
    }

    g_device_open = TRUE;
    printf("Clipboard: initialized (iffparse + clipboard.device)\n");
}

/*
 * Cleanup clipboard resources.
 */
void clip_cleanup(void)
{
    if (g_device_open) {
        CloseDevice((struct IORequest *)g_clipIO);
        g_device_open = FALSE;
    }
    if (g_clipIO) {
        DeleteIORequest((struct IORequest *)g_clipIO);
        g_clipIO = NULL;
    }
    if (g_clipPort) {
        DeleteMsgPort(g_clipPort);
        g_clipPort = NULL;
    }
    if (IFFParseBase) {
        CloseLibrary(IFFParseBase);
        IFFParseBase = NULL;
    }
    printf("Clipboard: cleaned up\n");
}

/*
 * Sanitize text for protocol output.
 * Replaces pipe characters and control chars with spaces.
 */
static void sanitize_text(char *text, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        if (text[i] == '|' || text[i] == '\r') {
            text[i] = ' ';
        } else if (text[i] == '\n') {
            text[i] = ' ';
        } else if (text[i] < 0x20 && text[i] != '\0') {
            text[i] = ' ';
        }
    }
}

/*
 * Read text content from clipboard.
 * Command: CLIPGET
 *
 * Opens an IFF read handle on the clipboard, looks for
 * FTXT/CHRS chunks, and returns the text content.
 *
 * Response: CLIPBOARD|length|text_content
 */
void clip_handle_get(void)
{
    static char linebuf[BRIDGE_MAX_LINE];
    static char textbuf[1024];
    struct IFFHandle *iff;
    struct ContextNode *cn;
    LONG err;
    LONG textLen = 0;

    if (!IFFParseBase || !g_device_open) {
        protocol_send_raw("ERR|CLIPBOARD|iffparse or clipboard not available");
        return;
    }

    iff = AllocIFF();
    if (!iff) {
        protocol_send_raw("ERR|CLIPBOARD|Out of memory");
        return;
    }

    /* Attach clipboard to IFF handle */
    iff->iff_Stream = (ULONG)g_clipIO;
    InitIFFasClip(iff);

    err = OpenIFF(iff, IFFF_READ);
    if (err != 0) {
        FreeIFF(iff);
        /* Empty clipboard */
        protocol_send_raw("CLIPBOARD|0|");
        return;
    }

    /* Tell parser to stop at FTXT CHRS chunks */
    err = StopChunk(iff, ID_FTXT, ID_CHRS);
    if (err != 0) {
        CloseIFF(iff);
        FreeIFF(iff);
        protocol_send_raw("CLIPBOARD|0|");
        return;
    }

    /* Parse until we find CHRS or end */
    textLen = 0;
    while (1) {
        err = ParseIFF(iff, IFFPARSE_SCAN);
        if (err != 0) break;

        cn = CurrentChunk(iff);
        if (!cn) continue;

        if (cn->cn_Type == ID_FTXT && cn->cn_ID == ID_CHRS) {
            LONG toRead = cn->cn_Size;
            LONG bytesRead;

            if (toRead > (LONG)(sizeof(textbuf) - 1 - textLen)) {
                toRead = (LONG)(sizeof(textbuf) - 1 - textLen);
            }
            if (toRead <= 0) break;

            bytesRead = ReadChunkBytes(iff, textbuf + textLen, toRead);
            if (bytesRead > 0) {
                textLen += bytesRead;
            }
            break;  /* Only read first CHRS chunk */
        }
    }

    CloseIFF(iff);
    FreeIFF(iff);

    textbuf[textLen] = '\0';

    /* Sanitize for protocol transport */
    sanitize_text(textbuf, textLen);

    sprintf(linebuf, "CLIPBOARD|%ld|%s", (long)textLen, textbuf);
    protocol_send_raw(linebuf);
}

/*
 * Write text content to clipboard.
 * Command: CLIPSET|text
 *
 * Opens an IFF write handle on the clipboard and writes
 * an FTXT form with a CHRS chunk containing the text.
 *
 * Response: OK|CLIPBOARD|set N bytes
 */
void clip_handle_set(const char *args)
{
    static char linebuf[BRIDGE_MAX_LINE];
    struct IFFHandle *iff;
    LONG err;
    LONG textLen;

    if (!IFFParseBase || !g_device_open) {
        protocol_send_raw("ERR|CLIPBOARD|iffparse or clipboard not available");
        return;
    }

    if (!args || !args[0]) {
        protocol_send_raw("ERR|CLIPBOARD|No text provided");
        return;
    }

    textLen = strlen(args);
    if (textLen > 1024) textLen = 1024;

    iff = AllocIFF();
    if (!iff) {
        protocol_send_raw("ERR|CLIPBOARD|Out of memory");
        return;
    }

    iff->iff_Stream = (ULONG)g_clipIO;
    InitIFFasClip(iff);

    err = OpenIFF(iff, IFFF_WRITE);
    if (err != 0) {
        FreeIFF(iff);
        protocol_send_raw("ERR|CLIPBOARD|Failed to open IFF for write");
        return;
    }

    /* Push FTXT form */
    err = PushChunk(iff, ID_FTXT, ID_FORM, IFFSIZE_UNKNOWN);
    if (err != 0) {
        CloseIFF(iff);
        FreeIFF(iff);
        protocol_send_raw("ERR|CLIPBOARD|Failed to push FTXT form");
        return;
    }

    /* Push CHRS chunk */
    err = PushChunk(iff, 0, ID_CHRS, IFFSIZE_UNKNOWN);
    if (err != 0) {
        PopChunk(iff);
        CloseIFF(iff);
        FreeIFF(iff);
        protocol_send_raw("ERR|CLIPBOARD|Failed to push CHRS chunk");
        return;
    }

    /* Write text data */
    err = WriteChunkBytes(iff, (APTR)args, textLen);
    if (err != textLen) {
        PopChunk(iff);
        PopChunk(iff);
        CloseIFF(iff);
        FreeIFF(iff);
        protocol_send_raw("ERR|CLIPBOARD|Write failed");
        return;
    }

    /* Close CHRS chunk */
    err = PopChunk(iff);
    if (err != 0) {
        PopChunk(iff);
        CloseIFF(iff);
        FreeIFF(iff);
        protocol_send_raw("ERR|CLIPBOARD|Failed to close CHRS chunk");
        return;
    }

    /* Close FTXT form */
    err = PopChunk(iff);
    if (err != 0) {
        CloseIFF(iff);
        FreeIFF(iff);
        protocol_send_raw("ERR|CLIPBOARD|Failed to close FTXT form");
        return;
    }

    CloseIFF(iff);
    FreeIFF(iff);

    sprintf(linebuf, "OK|CLIPBOARD|set %ld bytes", (long)textLen);
    protocol_send_raw(linebuf);
}
