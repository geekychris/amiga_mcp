/*
 * intuition_inspector.c - Intuition screen/window/gadget inspection
 *
 * Provides inspection of Intuition structures: screens, windows, and gadgets
 * without requiring a client application. Also provides window management
 * (activate, front/back, move, resize) and screen management.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_internal.h"

extern struct IntuitionBase *IntuitionBase;
extern struct GfxBase *GfxBase;

/* Safe buffer append: appends src to buf at *pos, respecting bufSize.
 * Returns 1 if appended, 0 if it didn't fit. */
static int buf_append(char *buf, int *pos, int bufSize, const char *src)
{
    int len = strlen(src);
    if (*pos + len >= bufSize - 1) return 0;
    memcpy(buf + *pos, src, len);
    *pos += len;
    buf[*pos] = '\0';
    return 1;
}

static int buf_append_char(char *buf, int *pos, int bufSize, char ch)
{
    if (*pos + 1 >= bufSize - 1) return 0;
    buf[*pos] = ch;
    (*pos)++;
    buf[*pos] = '\0';
    return 1;
}

/*
 * Sanitize a text string for protocol output.
 * Replaces pipe, comma, colon, newline, and carriage return with spaces.
 * Validates pointer before dereferencing.
 */
static void sanitize_text(char *dst, const char *src, int maxLen)
{
    int i, len;
    if (!src || (ULONG)src < 0x100) {
        dst[0] = '\0';
        return;
    }
    len = strlen(src);
    if (len > maxLen) len = maxLen;
    for (i = 0; i < len; i++) {
        char c = src[i];
        if (c == '|' || c == ',' || c == ':' || c == '\n' || c == '\r') c = ' ';
        dst[i] = c;
    }
    dst[len] = '\0';
}

/*
 * List all screens.
 * Command: LISTSCREENS
 *
 * Walk IntuitionBase->FirstScreen linked list.
 * Response: SCREENS|count|title:width:height:depth:viewmodes:flags:addr,...
 */
void intui_handle_screens(void)
{
    struct Screen *scr;
    static char linebuf[BRIDGE_MAX_LINE];
    char entry[128];
    char titlebuf[42];
    char countStr[16];
    int pos = 0;
    int count = 0;
    int headerPos;

    if (!IntuitionBase) {
        protocol_send_raw("ERR|SCREENS|IntuitionBase not open");
        return;
    }

    sprintf(linebuf, "SCREENS|");
    pos = strlen(linebuf);
    headerPos = pos;

    /* Reserve space for count (up to "9999|" = 5 chars) */
    memset(linebuf + pos, ' ', 5);
    pos += 5;
    linebuf[pos] = '\0';

    Forbid();

    for (scr = IntuitionBase->FirstScreen; scr; scr = scr->NextScreen) {
        /* Validate screen pointer */
        if ((ULONG)scr < 0x100 || (ULONG)scr > 0x10000000) break;

        sanitize_text(titlebuf, (const char *)scr->Title, 40);
        if (titlebuf[0] == '\0') {
            strcpy(titlebuf, "(untitled)");
        }

        {
            long depth = 0;
            if (scr->RastPort.BitMap &&
                (ULONG)scr->RastPort.BitMap >= 0x100 &&
                (ULONG)scr->RastPort.BitMap < 0x10000000) {
                depth = (long)scr->RastPort.BitMap->Depth;
            }
            sprintf(entry, "%s:%ld:%ld:%ld:%lx:%lx:%lx",
                    titlebuf,
                    (long)scr->Width,
                    (long)scr->Height,
                    depth,
                    (unsigned long)scr->ViewPort.Modes,
                    (unsigned long)scr->Flags,
                    (unsigned long)scr);
        }
        entry[sizeof(entry) - 1] = '\0';

        if (count > 0) {
            if (!buf_append_char(linebuf, &pos, BRIDGE_MAX_LINE, ',')) break;
        }
        if (!buf_append(linebuf, &pos, BRIDGE_MAX_LINE, entry)) break;
        count++;
    }

    Permit();

    /* Patch in the count */
    sprintf(countStr, "%ld|", (long)count);
    {
        int clen = strlen(countStr);
        int gap = 5 - clen;
        if (gap > 0) {
            int entryStart = headerPos + 5;
            int entryLen = pos - entryStart;
            memmove(linebuf + headerPos + clen, linebuf + entryStart, entryLen);
            pos -= gap;
        }
        memcpy(linebuf + headerPos, countStr, clen);
    }

    linebuf[pos] = '\0';
    protocol_send_raw(linebuf);
}

/*
 * List all windows on a given screen.
 * Command: LISTWINDOWS2|screen_addr_hex  (or empty for first screen)
 *
 * Response: WINDOWS|screenAddr|count|title:left:top:width:height:flags:idcmp:addr,...
 */
void intui_handle_windows(const char *args)
{
    struct Screen *scr;
    struct Window *win;
    static char linebuf[BRIDGE_MAX_LINE];
    char entry[128];
    char titlebuf[42];
    char countStr[16];
    int pos = 0;
    int count = 0;
    int headerPos;
    ULONG scrAddr;

    if (!IntuitionBase) {
        protocol_send_raw("ERR|WINDOWS|IntuitionBase not open");
        return;
    }

    Forbid();

    /* Parse screen address or use first screen */
    if (args && args[0] != '\0') {
        scrAddr = strtoul(args, NULL, 16);
        if (scrAddr < 0x100 || scrAddr > 0x10000000) {
            Permit();
            protocol_send_raw("ERR|WINDOWS|Invalid screen address");
            return;
        }
        /* Validate the address is actually a screen by walking the list */
        scr = NULL;
        {
            struct Screen *s;
            for (s = IntuitionBase->FirstScreen; s; s = s->NextScreen) {
                if ((ULONG)s == scrAddr) {
                    scr = s;
                    break;
                }
            }
        }
        if (!scr) {
            Permit();
            protocol_send_raw("ERR|WINDOWS|Screen not found at given address");
            return;
        }
    } else {
        scr = IntuitionBase->FirstScreen;
        if (!scr) {
            Permit();
            protocol_send_raw("ERR|WINDOWS|No screen found");
            return;
        }
        scrAddr = (ULONG)scr;
    }

    sprintf(linebuf, "WINDOWS|%lx|", (unsigned long)scrAddr);
    pos = strlen(linebuf);
    headerPos = pos;

    /* Reserve space for count */
    memset(linebuf + pos, ' ', 5);
    pos += 5;
    linebuf[pos] = '\0';

    for (win = scr->FirstWindow; win; win = win->NextWindow) {
        /* Validate window pointer */
        if ((ULONG)win < 0x100 || (ULONG)win > 0x10000000) break;

        sanitize_text(titlebuf, (const char *)win->Title, 40);
        if (titlebuf[0] == '\0') {
            strcpy(titlebuf, "(untitled)");
        }

        sprintf(entry, "%s:%ld:%ld:%ld:%ld:%lx:%lx:%lx",
                titlebuf,
                (long)win->LeftEdge,
                (long)win->TopEdge,
                (long)win->Width,
                (long)win->Height,
                (unsigned long)win->Flags,
                (unsigned long)win->IDCMPFlags,
                (unsigned long)win);
        entry[sizeof(entry) - 1] = '\0';

        if (count > 0) {
            if (!buf_append_char(linebuf, &pos, BRIDGE_MAX_LINE, ',')) break;
        }
        if (!buf_append(linebuf, &pos, BRIDGE_MAX_LINE, entry)) break;
        count++;
    }

    Permit();

    /* Patch in the count */
    sprintf(countStr, "%ld|", (long)count);
    {
        int clen = strlen(countStr);
        int gap = 5 - clen;
        if (gap > 0) {
            int entryStart = headerPos + 5;
            int entryLen = pos - entryStart;
            memmove(linebuf + headerPos + clen, linebuf + entryStart, entryLen);
            pos -= gap;
        }
        memcpy(linebuf + headerPos, countStr, clen);
    }

    linebuf[pos] = '\0';
    protocol_send_raw(linebuf);
}

/*
 * List gadgets for a given window.
 * Command: LISTGADGETS|window_addr_hex
 *
 * Response: GADGETS|windowAddr|count|id:left:top:width:height:type:flags:text:addr,...
 */
void intui_handle_gadgets(const char *args)
{
    struct Screen *scr;
    struct Window *win = NULL;
    struct Gadget *gad;
    static char linebuf[BRIDGE_MAX_LINE];
    char entry[128];
    char textbuf[42];
    char countStr[16];
    int pos = 0;
    int count = 0;
    int headerPos;
    ULONG winAddr;

    if (!IntuitionBase) {
        protocol_send_raw("ERR|GADGETS|IntuitionBase not open");
        return;
    }

    if (!args || args[0] == '\0') {
        protocol_send_raw("ERR|GADGETS|Missing window address");
        return;
    }

    winAddr = strtoul(args, NULL, 16);
    if (winAddr < 0x100 || winAddr > 0x10000000) {
        protocol_send_raw("ERR|GADGETS|Invalid window address");
        return;
    }

    Forbid();

    /* Validate the address is actually a window by walking all screens/windows */
    for (scr = IntuitionBase->FirstScreen; scr; scr = scr->NextScreen) {
        struct Window *w;
        if ((ULONG)scr < 0x100 || (ULONG)scr > 0x10000000) break;
        for (w = scr->FirstWindow; w; w = w->NextWindow) {
            if ((ULONG)w < 0x100 || (ULONG)w > 0x10000000) break;
            if ((ULONG)w == winAddr) {
                win = w;
                break;
            }
        }
        if (win) break;
    }

    if (!win) {
        Permit();
        protocol_send_raw("ERR|GADGETS|Window not found at given address");
        return;
    }

    sprintf(linebuf, "GADGETS|%lx|", (unsigned long)winAddr);
    pos = strlen(linebuf);
    headerPos = pos;

    /* Reserve space for count */
    memset(linebuf + pos, ' ', 5);
    pos += 5;
    linebuf[pos] = '\0';

    for (gad = win->FirstGadget; gad; gad = gad->NextGadget) {
        /* Validate gadget pointer */
        if ((ULONG)gad < 0x100 || (ULONG)gad > 0x10000000) break;

        /* Extract gadget text if available */
        if (gad->GadgetText &&
            (ULONG)gad->GadgetText >= 0x100 &&
            (ULONG)gad->GadgetText < 0x10000000) {
            sanitize_text(textbuf, (const char *)gad->GadgetText->IText, 40);
            if (textbuf[0] == '\0') {
                strcpy(textbuf, "n/a");
            }
        } else {
            strcpy(textbuf, "n/a");
        }

        sprintf(entry, "%ld:%ld:%ld:%ld:%ld:%ld:%ld:%s:%lx",
                (long)gad->GadgetID,
                (long)gad->LeftEdge,
                (long)gad->TopEdge,
                (long)gad->Width,
                (long)gad->Height,
                (long)gad->GadgetType,
                (long)gad->Flags,
                textbuf,
                (unsigned long)gad);
        entry[sizeof(entry) - 1] = '\0';

        if (count > 0) {
            if (!buf_append_char(linebuf, &pos, BRIDGE_MAX_LINE, ',')) break;
        }
        if (!buf_append(linebuf, &pos, BRIDGE_MAX_LINE, entry)) break;
        count++;
    }

    Permit();

    /* Patch in the count */
    sprintf(countStr, "%ld|", (long)count);
    {
        int clen = strlen(countStr);
        int gap = 5 - clen;
        if (gap > 0) {
            int entryStart = headerPos + 5;
            int entryLen = pos - entryStart;
            memmove(linebuf + headerPos + clen, linebuf + entryStart, entryLen);
            pos -= gap;
        }
        memcpy(linebuf + headerPos, countStr, clen);
    }

    linebuf[pos] = '\0';
    protocol_send_raw(linebuf);
}

/* ─── Window/Screen Management ─── */

/*
 * Find a window by address, validating it exists in the Intuition list.
 * Must be called inside Forbid()/Permit().
 */
static struct Window *find_window_by_addr(ULONG winAddr)
{
    struct Screen *scr;
    struct Window *w;

    if (winAddr < 0x100 || winAddr > 0x10000000) return NULL;

    for (scr = IntuitionBase->FirstScreen; scr; scr = scr->NextScreen) {
        if ((ULONG)scr < 0x100 || (ULONG)scr > 0x10000000) break;
        for (w = scr->FirstWindow; w; w = w->NextWindow) {
            if ((ULONG)w < 0x100 || (ULONG)w > 0x10000000) break;
            if ((ULONG)w == winAddr) return w;
        }
    }
    return NULL;
}

static struct Screen *find_screen_by_addr(ULONG scrAddr)
{
    struct Screen *s;

    if (scrAddr < 0x100 || scrAddr > 0x10000000) return NULL;

    for (s = IntuitionBase->FirstScreen; s; s = s->NextScreen) {
        if ((ULONG)s < 0x100 || (ULONG)s > 0x10000000) break;
        if ((ULONG)s == scrAddr) return s;
    }
    return NULL;
}

/*
 * Activate a window (give it focus).
 * Command: WINACTIVATE|window_addr_hex
 */
void intui_handle_activate(const char *args)
{
    struct Window *win;
    ULONG winAddr;

    if (!IntuitionBase || !args || args[0] == '\0') {
        protocol_send_raw("ERR|WINACTIVATE|Missing window address");
        return;
    }

    winAddr = strtoul(args, NULL, 16);
    Forbid();
    win = find_window_by_addr(winAddr);
    Permit();

    if (!win) {
        protocol_send_raw("ERR|WINACTIVATE|Window not found");
        return;
    }

    ActivateWindow(win);
    protocol_send_raw("OK|WINACTIVATE|activated");
}

/*
 * Bring a window to front.
 * Command: WINTOFRONT|window_addr_hex
 */
void intui_handle_tofront(const char *args)
{
    struct Window *win;
    ULONG winAddr;

    if (!IntuitionBase || !args || args[0] == '\0') {
        protocol_send_raw("ERR|WINTOFRONT|Missing window address");
        return;
    }

    winAddr = strtoul(args, NULL, 16);
    Forbid();
    win = find_window_by_addr(winAddr);
    Permit();

    if (!win) {
        protocol_send_raw("ERR|WINTOFRONT|Window not found");
        return;
    }

    WindowToFront(win);
    protocol_send_raw("OK|WINTOFRONT|done");
}

/*
 * Send a window to back.
 * Command: WINTOBACK|window_addr_hex
 */
void intui_handle_toback(const char *args)
{
    struct Window *win;
    ULONG winAddr;

    if (!IntuitionBase || !args || args[0] == '\0') {
        protocol_send_raw("ERR|WINTOBACK|Missing window address");
        return;
    }

    winAddr = strtoul(args, NULL, 16);
    Forbid();
    win = find_window_by_addr(winAddr);
    Permit();

    if (!win) {
        protocol_send_raw("ERR|WINTOBACK|Window not found");
        return;
    }

    WindowToBack(win);
    protocol_send_raw("OK|WINTOBACK|done");
}

/*
 * Zip (toggle min/max size) a window.
 * Command: WINZIP|window_addr_hex
 */
void intui_handle_zip(const char *args)
{
    struct Window *win;
    ULONG winAddr;

    if (!IntuitionBase || !args || args[0] == '\0') {
        protocol_send_raw("ERR|WINZIP|Missing window address");
        return;
    }

    winAddr = strtoul(args, NULL, 16);
    Forbid();
    win = find_window_by_addr(winAddr);
    Permit();

    if (!win) {
        protocol_send_raw("ERR|WINZIP|Window not found");
        return;
    }

    ZipWindow(win);
    protocol_send_raw("OK|WINZIP|done");
}

/*
 * Move a window to a specific position.
 * Command: WINMOVE|window_addr_hex|x|y
 */
void intui_handle_move(const char *args)
{
    struct Window *win;
    ULONG winAddr;
    long newx, newy;
    char *p;

    if (!IntuitionBase || !args || args[0] == '\0') {
        protocol_send_raw("ERR|WINMOVE|Missing arguments");
        return;
    }

    winAddr = strtoul(args, NULL, 16);
    p = strchr(args, '|');
    if (!p) {
        protocol_send_raw("ERR|WINMOVE|Missing x|y");
        return;
    }
    newx = strtol(p + 1, &p, 10);
    if (!p || *p != '|') {
        protocol_send_raw("ERR|WINMOVE|Missing y");
        return;
    }
    newy = strtol(p + 1, NULL, 10);

    Forbid();
    win = find_window_by_addr(winAddr);
    Permit();

    if (!win) {
        protocol_send_raw("ERR|WINMOVE|Window not found");
        return;
    }

    MoveWindow(win, newx - (long)win->LeftEdge, newy - (long)win->TopEdge);
    protocol_send_raw("OK|WINMOVE|done");
}

/*
 * Resize a window.
 * Command: WINSIZE|window_addr_hex|width|height
 */
void intui_handle_size(const char *args)
{
    struct Window *win;
    ULONG winAddr;
    long w, h;
    char *p;

    if (!IntuitionBase || !args || args[0] == '\0') {
        protocol_send_raw("ERR|WINSIZE|Missing arguments");
        return;
    }

    winAddr = strtoul(args, NULL, 16);
    p = strchr(args, '|');
    if (!p) {
        protocol_send_raw("ERR|WINSIZE|Missing width|height");
        return;
    }
    w = strtol(p + 1, &p, 10);
    if (!p || *p != '|') {
        protocol_send_raw("ERR|WINSIZE|Missing height");
        return;
    }
    h = strtol(p + 1, NULL, 10);

    Forbid();
    win = find_window_by_addr(winAddr);
    Permit();

    if (!win) {
        protocol_send_raw("ERR|WINSIZE|Window not found");
        return;
    }

    SizeWindow(win, w - (long)win->Width, h - (long)win->Height);
    protocol_send_raw("OK|WINSIZE|done");
}

/*
 * Bring a screen to front.
 * Command: SCRTOFRONT|screen_addr_hex
 */
void intui_handle_scrtofront(const char *args)
{
    struct Screen *scr;
    ULONG scrAddr;

    if (!IntuitionBase || !args || args[0] == '\0') {
        protocol_send_raw("ERR|SCRTOFRONT|Missing screen address");
        return;
    }

    scrAddr = strtoul(args, NULL, 16);
    Forbid();
    scr = find_screen_by_addr(scrAddr);
    Permit();

    if (!scr) {
        protocol_send_raw("ERR|SCRTOFRONT|Screen not found");
        return;
    }

    ScreenToFront(scr);
    protocol_send_raw("OK|SCRTOFRONT|done");
}

/*
 * Send a screen to back.
 * Command: SCRTOBACK|screen_addr_hex
 */
void intui_handle_scrtoback(const char *args)
{
    struct Screen *scr;
    ULONG scrAddr;

    if (!IntuitionBase || !args || args[0] == '\0') {
        protocol_send_raw("ERR|SCRTOBACK|Missing screen address");
        return;
    }

    scrAddr = strtoul(args, NULL, 16);
    Forbid();
    scr = find_screen_by_addr(scrAddr);
    Permit();

    if (!scr) {
        protocol_send_raw("ERR|SCRTOBACK|Screen not found");
        return;
    }

    ScreenToBack(scr);
    protocol_send_raw("OK|SCRTOBACK|done");
}
