/*
 * gfx_inspector.c - Amiga graphics inspection
 *
 * Provides screenshot capture, palette viewer/editor,
 * copper list viewer, and sprite inspector.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/view.h>
#include <graphics/gfxbase.h>
#include <graphics/copper.h>
#include <graphics/sprite.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_internal.h"

extern struct IntuitionBase *IntuitionBase;
extern struct GfxBase *GfxBase;

static const char hex_chars[] = "0123456789abcdef";

/* --- Screenshot output sink ---------------------------------------------
 * The capture loop must run under LockIBase (the screen pixels have to be
 * stable), but streaming the result over a slow transport — 115200 serial is
 * ~2-3 minutes for a full screen — must NOT hold that lock, or all of
 * Intuition freezes for the whole transfer. So we buffer the formatted
 * protocol lines during the locked capture and drain them AFTER unlocking.
 * Three tiers, chosen by what the machine can spare:
 *   RAM    - hold the whole thing in fast/any RAM (any roomy machine: always)
 *   FILE   - spool to a T: temp file when RAM is too tight (helps iff T: is on
 *            real storage, e.g. a hard disk)
 *   DIRECT - neither available (e.g. a low-mem box where T: is RAM:, or only a
 *            floppy): fall back to sending under the lock — a brief freeze, but
 *            correct. */
#define SCRSINK_RAM    0
#define SCRSINK_FILE   1
#define SCRSINK_DIRECT 2

struct ScrSink {
    int   mode;
    UBYTE *ram;
    ULONG  cap;
    ULONG  len;
    BPTR   fh;
};

static const char SCRSINK_CACHE[] = "T:ab_scrshot.cache";

/* estBytes: a generous upper bound on the total formatted output. */
static void scrsink_init(struct ScrSink *s, ULONG estBytes)
{
    s->mode = SCRSINK_DIRECT;
    s->ram = NULL; s->cap = 0; s->len = 0; s->fh = 0;

    if (estBytes < 4096) estBytes = 4096;
    /* Tier 1: RAM. Leave 256K headroom so we never grab the last of free mem
     * (which would itself wedge the system). */
    if (AvailMem(MEMF_ANY) > estBytes + (256UL * 1024)) {
        s->ram = AllocMem(estBytes, MEMF_ANY);
    }
    if (s->ram) { s->mode = SCRSINK_RAM; s->cap = estBytes; return; }

    /* Tier 2: disk spool (only a win if T: lives on real storage, not RAM:). */
    s->fh = Open((CONST_STRPTR)SCRSINK_CACHE, MODE_NEWFILE);
    if (s->fh) { s->mode = SCRSINK_FILE; return; }

    /* Tier 3: DIRECT — scrsink_line() will send under the lock. */
}

/* Emit one already-formatted protocol line (no trailing newline). */
static void scrsink_line(struct ScrSink *s, const char *line)
{
    ULONG n = (ULONG)strlen(line);
    if (s->mode == SCRSINK_RAM) {
        if (s->len + n + 1 <= s->cap) {
            CopyMem((APTR)line, s->ram + s->len, (LONG)n);
            s->ram[s->len + n] = '\n';
            s->len += n + 1;
        } else {
            protocol_send_raw(line);   /* estimate too small: don't lose the row */
        }
    } else if (s->mode == SCRSINK_FILE) {
        Write(s->fh, (APTR)line, (LONG)n);
        Write(s->fh, (APTR)"\n", 1);
    } else {
        protocol_send_raw(line);       /* DIRECT: under the lock */
    }
}

/* Call AFTER UnlockIBase: stream the buffered lines, then release resources. */
static void scrsink_drain(struct ScrSink *s)
{
    if (s->mode == SCRSINK_RAM) {
        ULONG i, start = 0;
        for (i = 0; i < s->len; i++) {
            if (s->ram[i] == '\n') {
                s->ram[i] = '\0';
                protocol_send_raw((char *)(s->ram + start));
                start = i + 1;
            }
        }
        FreeMem(s->ram, s->cap);
        s->ram = NULL;
    } else if (s->mode == SCRSINK_FILE) {
        BPTR rf;
        Close(s->fh);
        s->fh = 0;
        rf = Open((CONST_STRPTR)SCRSINK_CACHE, MODE_OLDFILE);
        if (rf) {
            static char rl[BRIDGE_MAX_LINE];
            while (FGets(rf, (STRPTR)rl, BRIDGE_MAX_LINE)) {
                int L = (int)strlen(rl);
                while (L > 0 && (rl[L-1] == '\n' || rl[L-1] == '\r')) rl[--L] = '\0';
                protocol_send_raw(rl);
            }
            Close(rf);
        }
        DeleteFile((CONST_STRPTR)SCRSINK_CACHE);
    }
    /* DIRECT: nothing buffered, already sent. */
}

/*
 * Hex-encode a byte buffer into a string.
 * outHex must have space for len*2+1 bytes.
 */
static void hex_encode(const UBYTE *data, ULONG len, char *outHex)
{
    ULONG i;
    for (i = 0; i < len; i++) {
        outHex[i * 2]     = hex_chars[(data[i] >> 4) & 0x0F];
        outHex[i * 2 + 1] = hex_chars[data[i] & 0x0F];
    }
    outHex[len * 2] = '\0';
}

/* Compare two pixels of `bpp` bytes. */
static int pix_eq(const UBYTE *a, const UBYTE *b, int bpp)
{
    int i;
    for (i = 0; i < bpp; i++) if (a[i] != b[i]) return 0;
    return 1;
}

/*
 * TGA/PackBits-style RLE encode `count` pixels of `bpp` bytes each.
 *
 * Output is a sequence of packets, each starting with one header byte:
 *   high bit SET  -> run:     (hdr & 0x7f)+1 copies of the following 1 pixel
 *   high bit CLEAR-> literal: (hdr & 0x7f)+1 pixels follow verbatim
 * so incompressible data costs at most 1 header byte per 128 pixels.
 *
 * `out` must hold up to count*bpp + count/128 + 1 bytes.
 * Returns the number of bytes written.
 */
static ULONG rle_encode(const UBYTE *src, ULONG count, int bpp, UBYTE *out)
{
    ULONG ip = 0, op = 0;

    while (ip < count) {
        ULONG run = 1;
        while (ip + run < count && run < 128 &&
               pix_eq(src + (ip + run) * bpp, src + ip * bpp, bpp))
            run++;

        if (run >= 2) {
            int k;
            out[op++] = (UBYTE)(0x80 | (run - 1));
            for (k = 0; k < bpp; k++) out[op++] = src[ip * bpp + k];
            ip += run;
        } else {
            /* Literal: gather pixels until a >=2 run begins or we hit 128. */
            ULONG lit = 1, k;
            while (ip + lit < count && lit < 128) {
                if (ip + lit + 1 < count &&
                    pix_eq(src + (ip + lit) * bpp,
                           src + (ip + lit + 1) * bpp, bpp))
                    break;
                lit++;
            }
            out[op++] = (UBYTE)(lit - 1);
            for (k = 0; k < lit * (ULONG)bpp; k++) out[op++] = src[ip * bpp + k];
            ip += lit;
        }
    }
    return op;
}

/*
 * Find a window by title on any screen.
 * Must be called with IntuitionBase locked.
 */
static struct Window *find_window_by_title(const char *title)
{
    struct Screen *scr;
    struct Window *win;

    for (scr = IntuitionBase->FirstScreen; scr; scr = scr->NextScreen) {
        for (win = scr->FirstWindow; win; win = win->NextWindow) {
            if (win->Title && strcmp((const char *)win->Title, title) == 0) {
                return win;
            }
        }
    }
    return NULL;
}

/*
 * Screenshot handler.
 * Command: SCREENSHOT or SCREENSHOT|window_title
 *
 * Response:
 *   SCRINFO|width|height|depth|r0g0b0,r1g1b1,...
 *   SCRDATA|row|plane|hex_data  (for each row and plane)
 */
void gfx_handle_screenshot(const char *args)
{
    ULONG lock;
    struct Screen *scr;
    struct BitMap *bm;
    struct ViewPort *vp;
    struct ColorMap *cm;
    UWORD width, height, depth;
    UWORD numColors;
    UWORD bytesPerRow;
    UWORD row, plane;
    /* Use static buffers to conserve stack */
    static char linebuf[BRIDGE_MAX_LINE];
    static char hexbuf[512];
    int pos;
    UWORD i;
    UWORD clipLeft = 0, clipTop = 0, clipWidth = 0, clipHeight = 0;
    BOOL useClip = FALSE;
    struct ScrSink sink;

    if (!IntuitionBase || !GfxBase) {
        protocol_send_raw("ERR|SCREENSHOT|Libraries not open");
        return;
    }

    lock = LockIBase(0);

    scr = IntuitionBase->FirstScreen;
    if (!scr) {
        UnlockIBase(lock);
        protocol_send_raw("ERR|SCREENSHOT|No screen found");
        return;
    }

    /* If window title given, find it and use its bounds */
    if (args && args[0] != '\0') {
        struct Window *win = find_window_by_title(args);
        if (win) {
            scr = win->WScreen;
            clipLeft = win->LeftEdge;
            clipTop = win->TopEdge;
            clipWidth = win->Width;
            clipHeight = win->Height;
            useClip = TRUE;
        }
        /* If window not found, just capture the front screen */
    }

    bm = scr->RastPort.BitMap;
    vp = &scr->ViewPort;
    cm = vp->ColorMap;
    width = scr->Width;
    height = scr->Height;
    depth = (UWORD)bm->Depth;

    if (useClip) {
        /* Clamp clip rect to screen bounds */
        if (clipLeft + clipWidth > width) clipWidth = width - clipLeft;
        if (clipTop + clipHeight > height) clipHeight = height - clipTop;
        width = clipWidth;
        height = clipHeight;
    }

    /* The BitMap.Depth field caps at 8 for deep RTG bitmaps, so query the real
     * depth. A true-colour (>8bpp) RTG screen has no palette - read it as RGB
     * via cybergraphics ReadPixelArray (RECTFMT_RGB converts BGRA32 -> RGB). */
    {
        ULONG realdepth = (ULONG)GetBitMapAttr(bm, BMA_DEPTH);
        ULONG bpr       = (ULONG)bm->BytesPerRow;

        if (realdepth > 8 && bm->Planes[0] && bpr >= (ULONG)width * 4) {
            /* True-colour (BGRA32) chunky framebuffer: read 4 bytes/pixel
             * straight from Planes[0] at the real stride, RLE-compress each
             * RGB row and emit it base64-encoded as a SCRRLE line. The width
             * is capped so even a worst-case (incompressible) RLE row stays
             * within BRIDGE_MAX_LINE after base64 expansion - base64's lower
             * overhead lets this cap cover 1600/1920 RTG modes that the old
             * hex path (cap 1344) would have truncated. */
            #define SCR_MAXW 1920
            static UBYTE rgbrow[SCR_MAXW * 3];
            static UBYTE rlebuf[SCR_MAXW * 3 + SCR_MAXW / 128 + 8];
            static char  rlehex[((SCR_MAXW * 3 + SCR_MAXW / 128 + 8) * 4) / 3 + 8];
            UWORD w = width;
            if (w > SCR_MAXW) w = SCR_MAXW;
            /* RGB after RLE+base64 is worst-case ~w*4 per row. */
            scrsink_init(&sink, ((ULONG)w * 4 + 32) * (ULONG)height + 256);
            sprintf(linebuf, "SCRINFO|%ld|%ld|24|", (long)w, (long)height);
            scrsink_line(&sink, linebuf);
            for (row = 0; row < height; row++) {
                UWORD srcRow = useClip ? (row + clipTop) : row;
                UWORD srcX   = useClip ? clipLeft : 0;
                UBYTE *src = (UBYTE *)bm->Planes[0]
                             + (ULONG)srcRow * bpr + (ULONG)srcX * 4;
                UWORD x;
                ULONG rlen;
                for (x = 0; x < w; x++) {
                    /* BGRA byte order -> RGB */
                    rgbrow[x * 3 + 0] = src[x * 4 + 2];  /* R */
                    rgbrow[x * 3 + 1] = src[x * 4 + 1];  /* G */
                    rgbrow[x * 3 + 2] = src[x * 4 + 0];  /* B */
                }
                rlen = rle_encode(rgbrow, (ULONG)w, 3, rlebuf);
                b64_encode(rlebuf, rlen, rlehex);   /* base64: 33% vs hex 100% */
                sprintf(linebuf, "SCRRLE|%ld|%s", (long)row, rlehex);
                scrsink_line(&sink, linebuf);
            }
            UnlockIBase(lock);          /* release Intuition BEFORE the slow send */
            scrsink_drain(&sink);
            return;
        }
        /* else fall through to the 8-bit / planar path */
    }

    numColors = 1;
    for (i = 0; i < depth; i++) numColors <<= 1;
    if (numColors > 256) numColors = 256;

    /* Build SCRINFO header with palette */
    sprintf(linebuf, "SCRINFO|%ld|%ld|%ld|",
            (long)width, (long)height, (long)depth);
    pos = strlen(linebuf);

    {
        /* True 8-bit-per-channel palette via GetRGB32 (the high byte of each
         * 32-bit fixed component is the 8-bit value). Sent as 6 hex digits
         * RRGGBB per colour. Falls back to zeros if there is no colormap. */
        static ULONG rgbtable[256 * 3];
        if (cm) {
            GetRGB32(cm, 0, (ULONG)numColors, rgbtable);
        } else {
            UWORD k;
            for (k = 0; k < numColors * 3; k++) rgbtable[k] = 0;
        }
        for (i = 0; i < numColors && pos < BRIDGE_MAX_LINE - 10; i++) {
            UBYTE r = (UBYTE)(rgbtable[i * 3 + 0] >> 24);
            UBYTE g = (UBYTE)(rgbtable[i * 3 + 1] >> 24);
            UBYTE b = (UBYTE)(rgbtable[i * 3 + 2] >> 24);
            if (i > 0) linebuf[pos++] = ',';
            linebuf[pos++] = hex_chars[(r >> 4) & 0x0F];
            linebuf[pos++] = hex_chars[r & 0x0F];
            linebuf[pos++] = hex_chars[(g >> 4) & 0x0F];
            linebuf[pos++] = hex_chars[g & 0x0F];
            linebuf[pos++] = hex_chars[(b >> 4) & 0x0F];
            linebuf[pos++] = hex_chars[b & 0x0F];
        }
    }
    linebuf[pos] = '\0';

    /* Buffer SCRINFO + all row data, then drain after UnlockIBase (below).
     * Estimate covers both the 8-bit chunky (~width*2/row) and planar
     * (~depth*520/row) paths generously. */
    scrsink_init(&sink, ((ULONG)width * 2 + (ULONG)depth * 520 + 64) * (ULONG)height + 256);
    scrsink_line(&sink, linebuf);

    /* Send bitplane data row by row, plane by plane.
     * Each row of a bitplane is bytesPerRow bytes.
     * For a full screen row: bytesPerRow = (width+15)/16 * 2
     * Max hex per line: 80 bytes = 160 hex chars (for 640-wide) */
    bytesPerRow = ((width + 15) / 16) * 2;

    /* RTG / chunky detection: a chunky (Picasso96/RTG) 8bpp bitmap stores one
     * pen byte per pixel, so BytesPerRow >= width; planar bitmaps have
     * BytesPerRow ~= width/8. A chunky framebuffer is NOT linear bitplanes, so
     * read it with ReadPixelArray8() (works on any bitmap type, planar or RTG),
     * which yields chunky pen indices. Each row is sent with plane=255 as a
     * sentinel for the host's chunky decoder. */
    if (depth <= 8 && (ULONG)bm->BytesPerRow >= (ULONG)width) {
        static char chunkhex[8200];
        static UBYTE rowbuf[4096];
        static UBYTE crle[4096 + 64];
        struct BitMap *tempbm;
        struct RastPort temprp;
        UWORD n = width;
        if (n > 4000) n = 4000;   /* keep the line under BRIDGE_MAX_LINE */
        tempbm = AllocBitMap(n, 1, depth, 0, bm);   /* 1-row scratch for RPA8 */
        if (tempbm) {
            InitRastPort(&temprp);
            temprp.BitMap = tempbm;
            for (row = 0; row < height; row++) {
                UWORD srcRow = useClip ? (row + clipTop) : row;
                UWORD srcX   = useClip ? clipLeft : 0;
                ULONG rlen;
                ReadPixelArray8(&scr->RastPort, srcX, srcRow,
                                (UWORD)(srcX + n - 1), srcRow, rowbuf, &temprp);
                /* RLE the 1-byte-per-pixel pen indices (host infers bpp=1
                 * from the <=8 depth in SCRINFO). */
                rlen = rle_encode(rowbuf, (ULONG)n, 1, crle);
                b64_encode(crle, rlen, chunkhex);   /* base64: 33% vs hex 100% */
                sprintf(linebuf, "SCRRLE|%ld|%s", (long)row, chunkhex);
                scrsink_line(&sink, linebuf);
            }
            FreeBitMap(tempbm);
            UnlockIBase(lock);          /* release Intuition BEFORE the slow send */
            scrsink_drain(&sink);
            return;
        }
        /* AllocBitMap failed: fall through to the planar path below */
    }

    for (row = 0; row < height; row++) {
        for (plane = 0; plane < depth; plane++) {
            UBYTE *planePtr;
            UWORD srcRow = useClip ? (row + clipTop) : row;
            UWORD srcByteOffset = useClip ? ((clipLeft / 8) & ~1) : 0;
            ULONG rowOffset;
            UWORD sendBytes;

            if (!bm->Planes[plane]) continue;

            rowOffset = (ULONG)srcRow * (ULONG)bm->BytesPerRow + srcByteOffset;
            planePtr = (UBYTE *)bm->Planes[plane] + rowOffset;
            sendBytes = bytesPerRow;

            /* Cap hex output to fit in line buffer:
             * "SCRDATA|row|plane|" is ~20 chars max, hex is 2*sendBytes */
            if (sendBytes > 240) sendBytes = 240;

            hex_encode(planePtr, sendBytes, hexbuf);
            sprintf(linebuf, "SCRDATA|%ld|%ld|%s",
                    (long)row, (long)plane, hexbuf);
            scrsink_line(&sink, linebuf);
        }
    }

    UnlockIBase(lock);                  /* release Intuition BEFORE the slow send */
    scrsink_drain(&sink);
}

/*
 * Palette read handler.
 * Command: PALETTE or PALETTE|screen_title
 *
 * Response: PALETTE|depth|r0g0b0,r1g1b1,...
 */
void gfx_handle_palette(const char *args)
{
    ULONG lock;
    struct Screen *scr;
    struct ViewPort *vp;
    struct ColorMap *cm;
    UWORD depth, numColors;
    static char linebuf[BRIDGE_MAX_LINE];
    int pos;
    UWORD i;

    if (!IntuitionBase || !GfxBase) {
        protocol_send_raw("ERR|PALETTE|Libraries not open");
        return;
    }

    lock = LockIBase(0);

    scr = IntuitionBase->FirstScreen;
    if (!scr) {
        UnlockIBase(lock);
        protocol_send_raw("ERR|PALETTE|No screen found");
        return;
    }

    vp = &scr->ViewPort;
    cm = vp->ColorMap;
    depth = scr->RastPort.BitMap->Depth;

    numColors = 1;
    for (i = 0; i < depth; i++) numColors <<= 1;
    if (numColors > 256) numColors = 256;

    sprintf(linebuf, "PALETTE|%ld|", (long)depth);
    pos = strlen(linebuf);

    {
        /* True 8-bit-per-channel palette via GetRGB32 (the high byte of each
         * 32-bit fixed component is the 8-bit value). Sent as 6 hex digits
         * RRGGBB per colour. Falls back to zeros if there is no colormap. */
        static ULONG rgbtable[256 * 3];
        if (cm) {
            GetRGB32(cm, 0, (ULONG)numColors, rgbtable);
        } else {
            UWORD k;
            for (k = 0; k < numColors * 3; k++) rgbtable[k] = 0;
        }
        for (i = 0; i < numColors && pos < BRIDGE_MAX_LINE - 10; i++) {
            UBYTE r = (UBYTE)(rgbtable[i * 3 + 0] >> 24);
            UBYTE g = (UBYTE)(rgbtable[i * 3 + 1] >> 24);
            UBYTE b = (UBYTE)(rgbtable[i * 3 + 2] >> 24);
            if (i > 0) linebuf[pos++] = ',';
            linebuf[pos++] = hex_chars[(r >> 4) & 0x0F];
            linebuf[pos++] = hex_chars[r & 0x0F];
            linebuf[pos++] = hex_chars[(g >> 4) & 0x0F];
            linebuf[pos++] = hex_chars[g & 0x0F];
            linebuf[pos++] = hex_chars[(b >> 4) & 0x0F];
            linebuf[pos++] = hex_chars[b & 0x0F];
        }
    }
    linebuf[pos] = '\0';

    UnlockIBase(lock);

    protocol_send_raw(linebuf);
}

/*
 * Palette set handler.
 * Command: SETPALETTE|index|rgb_hex
 * rgb_hex is a 3-char hex string: RGB (4-bit each)
 *
 * Response: OK|SETPALETTE|Color index set
 */
void gfx_handle_setpalette(const char *args)
{
    ULONG lock;
    struct Screen *scr;
    UWORD index;
    UWORD r, g, b;
    const char *sep;
    char hexbuf[4];

    if (!IntuitionBase || !GfxBase) {
        protocol_send_raw("ERR|SETPALETTE|Libraries not open");
        return;
    }

    if (!args || args[0] == '\0') {
        protocol_send_raw("ERR|SETPALETTE|needs index|rgb_hex");
        return;
    }

    /* Parse index */
    index = (UWORD)strtoul(args, NULL, 10);

    sep = strchr(args, '|');
    if (!sep || strlen(sep + 1) < 3) {
        protocol_send_raw("ERR|SETPALETTE|needs index|rgb_hex (3 hex digits)");
        return;
    }

    /* Parse RGB hex */
    hexbuf[0] = sep[1];
    hexbuf[1] = '\0';
    r = (UWORD)strtoul(hexbuf, NULL, 16);

    hexbuf[0] = sep[2];
    g = (UWORD)strtoul(hexbuf, NULL, 16);

    hexbuf[0] = sep[3];
    b = (UWORD)strtoul(hexbuf, NULL, 16);

    lock = LockIBase(0);
    scr = IntuitionBase->FirstScreen;
    if (!scr) {
        UnlockIBase(lock);
        protocol_send_raw("ERR|SETPALETTE|No screen found");
        return;
    }
    UnlockIBase(lock);

    /* SetRGB4 is safe to call without IBase lock */
    SetRGB4(&scr->ViewPort, (long)index, (long)r, (long)g, (long)b);

    protocol_send_raw("OK|SETPALETTE|Color set");
}

/*
 * Copper list viewer.
 * Command: COPPERLIST
 *
 * Response: COPPER|addr_hex|count|hex_data
 * (count = number of copper instructions, each is 4 bytes)
 * Sends multiple COPPER lines if data exceeds one line.
 */
void gfx_handle_copperlist(const char *args)
{
    struct cprlist *cpr;
    UWORD *copIns;
    ULONG numIns;
    ULONG addr;
    ULONG offset;
    static char linebuf[BRIDGE_MAX_LINE];
    static char hexbuf[512];

    if (!GfxBase) {
        protocol_send_raw("ERR|COPPERLIST|GfxBase not open");
        return;
    }

    /* Access the current View's copper list */
    if (!GfxBase->ActiView || !GfxBase->ActiView->LOFCprList) {
        protocol_send_raw("ERR|COPPERLIST|No active copper list");
        return;
    }

    cpr = GfxBase->ActiView->LOFCprList;
    copIns = (UWORD *)cpr->start;
    numIns = (ULONG)cpr->MaxCount;
    addr = (ULONG)copIns;

    if (!copIns || numIns == 0) {
        protocol_send_raw("ERR|COPPERLIST|Empty copper list");
        return;
    }

    /* Each copper instruction is 4 bytes (2 words).
     * Send in chunks that fit in our hex buffer (512 bytes = 256 data bytes = 64 instructions). */
    offset = 0;
    while (offset < numIns) {
        ULONG chunk = numIns - offset;
        ULONG byteCount;
        ULONG i;
        UBYTE *src;

        if (chunk > 60) chunk = 60;
        byteCount = chunk * 4;

        src = (UBYTE *)(copIns + offset * 2);
        hex_encode(src, byteCount, hexbuf);

        sprintf(linebuf, "COPPER|%08lx|%lu|%s",
                (unsigned long)(addr + offset * 4),
                (unsigned long)chunk,
                hexbuf);
        protocol_send_raw(linebuf);

        offset += chunk;

        /* Check for end-of-copperlist marker (WAIT $FFFF,$FFFE) */
        for (i = 0; i < chunk; i++) {
            ULONG idx = (offset - chunk + i) * 2;
            if (copIns[idx] == 0xFFFF && copIns[idx + 1] == 0xFFFE) {
                /* Found end marker, stop sending */
                return;
            }
        }
    }
}

/*
 * Sprite inspector.
 * Command: SPRITES
 *
 * Response: SPRITE|id|vstart|vstop|hstart|attached|hex_data
 * One line per sprite with valid data.
 *
 * Reads sprite data from the copper list by looking for SPRxPT moves.
 * Sprite data format:
 *   Word 0: VSTART.H7-H0 in high byte, HSTART.H8-H1 in low byte
 *   Word 1: VSTOP.H7-H0 in high byte, control bits in low byte
 *   Then pairs of words (plane 0, plane 1) for each scanline
 *   Terminated by two zero words.
 */
void gfx_handle_sprites(const char *args)
{
    ULONG sprPtrs[8];
    int sprFound[8];
    int i;
    static char linebuf[BRIDGE_MAX_LINE];
    static char hexbuf[512];

    if (!GfxBase) {
        protocol_send_raw("ERR|SPRITES|GfxBase not open");
        return;
    }

    /* Initialize */
    for (i = 0; i < 8; i++) {
        sprPtrs[i] = 0;
        sprFound[i] = 0;
    }

    /* Method 1: Read from GfxBase sprite pointers.
     * GfxBase->SpriteReserved gives info about reserved sprites.
     * The actual sprite data pointers are managed by the system
     * through SimpleSprite structures. We can also check the
     * ViewPort's sprite info via the copper list. */

    /* Method 2: Scan copper list for SPRxPT register moves */
    if (GfxBase->ActiView && GfxBase->ActiView->LOFCprList) {
        struct cprlist *cpr = GfxBase->ActiView->LOFCprList;
        UWORD *copIns = (UWORD *)cpr->start;
        ULONG numIns = (ULONG)cpr->MaxCount;
        ULONG ci;

        for (ci = 0; ci < numIns * 2; ci += 2) {
            UWORD reg = copIns[ci];
            UWORD val = copIns[ci + 1];

            if (reg == 0xFFFF && val == 0xFFFE) break;
            if (reg & 1) continue;  /* Not a MOVE */

            /* SPRxPTH: $0120, $0124, $0128, ..., $0138 */
            if (reg >= 0x0120 && reg <= 0x0138 && ((reg & 2) == 0)) {
                int idx = (reg - 0x0120) / 4;
                if (idx >= 0 && idx < 8) {
                    sprPtrs[idx] = (sprPtrs[idx] & 0x0000FFFF) | ((ULONG)val << 16);
                    sprFound[idx] |= 1;
                }
            }
            /* SPRxPTL: $0122, $0126, $012A, ..., $013A */
            if (reg >= 0x0122 && reg <= 0x013A && ((reg & 2) == 2)) {
                int idx = (reg - 0x0122) / 4;
                if (idx >= 0 && idx < 8) {
                    sprPtrs[idx] = (sprPtrs[idx] & 0xFFFF0000) | val;
                    sprFound[idx] |= 2;
                }
            }
        }
    }

    /* Method 3: Read sprite pointers from GfxBase->SimpleSprites array */
    if (GfxBase->SimpleSprites) {
        for (i = 0; i < 8; i++) {
            if (sprFound[i] == 3) continue;  /* Already found via copper */
            if (GfxBase->SimpleSprites[i] &&
                GfxBase->SimpleSprites[i]->posctldata) {
                sprPtrs[i] = (ULONG)GfxBase->SimpleSprites[i]->posctldata;
                sprFound[i] = 3;
            }
        }
    }

    /* Method 4: Read SPRxPT directly from custom chip registers.
     * The hardware registers at $DFF120-$DFF13E hold the current
     * DMA pointers. These are write-only on OCS but readable on
     * ECS/AGA via SPRXPOS/SPRXCTL readback. On emulators they
     * may be readable. Try reading the LOF copper list raw data
     * for any we still haven't found. */

    /* Emit SPRITE data for each found sprite */
    for (i = 0; i < 8; i++) {
        UWORD *sprData;
        UWORD vstart, vstop, hstart;
        UWORD ctrl0, ctrl1;
        BOOL attached;
        ULONG dataSize;
        ULONG maxBytes;
        UBYTE *dataBytes;

        if (sprFound[i] != 3) continue;
        if (sprPtrs[i] < 0x100 || sprPtrs[i] > 0x10000000) continue;

        /* Validate pointer is in known RAM */
        if (!TypeOfMem((APTR)sprPtrs[i])) continue;

        sprData = (UWORD *)sprPtrs[i];

        ctrl0 = sprData[0];
        ctrl1 = sprData[1];

        vstart = (ctrl0 >> 8) & 0xFF;
        hstart = (ctrl0 & 0xFF) << 1;
        vstop = (ctrl1 >> 8) & 0xFF;
        attached = (ctrl1 & 0x80) ? TRUE : FALSE;

        if (ctrl1 & 0x04) vstart |= 0x100;
        if (ctrl1 & 0x02) vstop |= 0x100;
        if (ctrl1 & 0x01) hstart |= 1;

        if (vstop > vstart) {
            dataSize = (ULONG)(vstop - vstart);
        } else {
            dataSize = 0;
        }

        maxBytes = (2 + dataSize * 2 + 2) * 2;
        if (maxBytes > 240) maxBytes = 240;

        dataBytes = (UBYTE *)sprData;
        hex_encode(dataBytes, maxBytes, hexbuf);

        sprintf(linebuf, "SPRITE|%ld|%ld|%ld|%ld|%ld|%s",
                (long)i,
                (long)vstart,
                (long)vstop,
                (long)hstart,
                (long)(attached ? 1 : 0),
                hexbuf);
        protocol_send_raw(linebuf);
    }
}

/*
 * List all open windows on all screens.
 * Command: LISTWINDOWS
 * Response: WINLIST|title1|title2|title3|...
 */
void gfx_handle_listwindows(const char *args)
{
    struct Screen *scr;
    struct Window *win;
    ULONG lock;
    static char linebuf[BRIDGE_MAX_LINE];
    int pos;

    (void)args;

    if (!IntuitionBase) {
        protocol_send_raw("ERR|LISTWINDOWS|IntuitionBase not open");
        return;
    }

    lock = LockIBase(0);

    strcpy(linebuf, "WINLIST");
    pos = 7;

    for (scr = IntuitionBase->FirstScreen; scr; scr = scr->NextScreen) {
        for (win = scr->FirstWindow; win; win = win->NextWindow) {
            const char *title = win->Title ? (const char *)win->Title : "(untitled)";
            int tlen = strlen(title);
            /* +1 for pipe separator */
            if (pos + 1 + tlen >= BRIDGE_MAX_LINE - 2) break;
            linebuf[pos++] = '|';
            memcpy(&linebuf[pos], title, tlen);
            pos += tlen;
        }
    }
    linebuf[pos] = '\0';

    UnlockIBase(lock);

    protocol_send_raw(linebuf);
}
