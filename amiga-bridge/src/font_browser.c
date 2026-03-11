/*
 * font_browser.c - Font enumeration and inspection
 *
 * Uses diskfont.library to list available fonts and
 * retrieve font metrics for the AmigaBridge daemon.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/text.h>
#include <libraries/diskfont.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/diskfont.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_internal.h"

extern struct GfxBase *GfxBase;

/*
 * Initialize font browser - open diskfont.library.
 */
void font_init(void)
{
    DiskfontBase = OpenLibrary((CONST_STRPTR)"diskfont.library", 36);
    if (DiskfontBase) {
        printf("Font browser: diskfont.library opened\n");
    } else {
        printf("Font browser: WARNING - diskfont.library not available\n");
    }
}

/*
 * Cleanup font browser - close diskfont.library.
 */
void font_cleanup(void)
{
    if (DiskfontBase) {
        CloseLibrary(DiskfontBase);
        DiskfontBase = NULL;
        printf("Font browser: diskfont.library closed\n");
    }
}

/*
 * List available fonts.
 * Command: LISTFONTS
 *
 * Uses AvailFonts() to enumerate fonts from both memory and disk.
 * Groups sizes by font family name.
 *
 * Response: FONTS|count|name1:size1,size2,...|name2:size1,...
 */
void font_handle_list(void)
{
    static char linebuf[BRIDGE_MAX_LINE];
    struct AvailFontsHeader *afh;
    struct AvailFonts *af;
    LONG bufSize;
    LONG result;
    int pos, count, i, nfonts;

    /* Track unique font families and their sizes */
    struct {
        char name[64];
        UWORD sizes[16];
        int sizeCount;
    } families[32];
    int familyCount = 0;

    if (!DiskfontBase) {
        protocol_send_raw("ERR|LISTFONTS|diskfont.library not available");
        return;
    }

    /* Start with a reasonable buffer; AvailFonts returns needed size if too small */
    bufSize = 4096;
    afh = (struct AvailFontsHeader *)AllocMem(bufSize, MEMF_CLEAR);
    if (!afh) {
        protocol_send_raw("ERR|LISTFONTS|Out of memory");
        return;
    }

    result = AvailFonts((STRPTR)afh, bufSize, AFF_DISK | AFF_MEMORY);
    if (result != 0) {
        /* Need more space - try once with a larger buffer */
        FreeMem(afh, bufSize);
        bufSize = 16384;
        afh = (struct AvailFontsHeader *)AllocMem(bufSize, MEMF_CLEAR);
        if (!afh) {
            protocol_send_raw("ERR|LISTFONTS|Out of memory");
            return;
        }
        result = AvailFonts((STRPTR)afh, bufSize, AFF_DISK | AFF_MEMORY);
        if (result != 0) {
            FreeMem(afh, bufSize);
            protocol_send_raw("ERR|LISTFONTS|Buffer too small");
            return;
        }
    }

    nfonts = (int)afh->afh_NumEntries;
    af = (struct AvailFonts *)((UBYTE *)afh + sizeof(struct AvailFontsHeader));

    /* Group fonts by family name */
    for (i = 0; i < nfonts; i++) {
        const char *name;
        UWORD size;
        int fi, si;
        BOOL found;

        if (!af[i].af_Attr.ta_Name) continue;

        name = (const char *)af[i].af_Attr.ta_Name;
        size = af[i].af_Attr.ta_YSize;

        /* Find or create family entry */
        found = FALSE;
        for (fi = 0; fi < familyCount; fi++) {
            if (strcmp(families[fi].name, name) == 0) {
                found = TRUE;
                break;
            }
        }

        if (!found) {
            if (familyCount >= 32) continue;
            fi = familyCount++;
            strncpy(families[fi].name, name, sizeof(families[fi].name) - 1);
            families[fi].name[sizeof(families[fi].name) - 1] = '\0';
            families[fi].sizeCount = 0;
        }

        /* Add size if not already present */
        found = FALSE;
        for (si = 0; si < families[fi].sizeCount; si++) {
            if (families[fi].sizes[si] == size) {
                found = TRUE;
                break;
            }
        }
        if (!found && families[fi].sizeCount < 16) {
            families[fi].sizes[families[fi].sizeCount++] = size;
        }
    }

    FreeMem(afh, bufSize);

    /* Build response */
    count = familyCount;
    sprintf(linebuf, "FONTS|%ld", (long)count);
    pos = strlen(linebuf);

    for (i = 0; i < familyCount && pos < BRIDGE_MAX_LINE - 80; i++) {
        int si;
        linebuf[pos++] = '|';

        /* Append font name */
        {
            int nlen = strlen(families[i].name);
            if (pos + nlen >= BRIDGE_MAX_LINE - 40) break;
            memcpy(&linebuf[pos], families[i].name, nlen);
            pos += nlen;
        }

        linebuf[pos++] = ':';

        /* Append sizes */
        for (si = 0; si < families[i].sizeCount && pos < BRIDGE_MAX_LINE - 10; si++) {
            char sizebuf[8];
            int slen;
            if (si > 0) linebuf[pos++] = ',';
            sprintf(sizebuf, "%ld", (long)families[i].sizes[si]);
            slen = strlen(sizebuf);
            memcpy(&linebuf[pos], sizebuf, slen);
            pos += slen;
        }
    }
    linebuf[pos] = '\0';

    protocol_send_raw(linebuf);
}

/*
 * Get info about a specific font.
 * Command: FONTINFO|fontname|size
 *
 * Opens the specified font and returns its metrics.
 *
 * Response: FONTINFO|name|size|ysize|xsize|style|flags|baseline
 */
void font_handle_info(const char *args)
{
    static char linebuf[BRIDGE_MAX_LINE];
    struct TextAttr ta;
    struct TextFont *tf;
    char namebuf[128];
    const char *sep;
    UWORD size;

    if (!args || !args[0]) {
        protocol_send_raw("ERR|FONTINFO|Missing arguments");
        return;
    }

    if (!DiskfontBase) {
        protocol_send_raw("ERR|FONTINFO|diskfont.library not available");
        return;
    }

    /* Parse fontname|size */
    sep = strchr(args, '|');
    if (!sep) {
        protocol_send_raw("ERR|FONTINFO|Missing size (use FONTINFO|name|size)");
        return;
    }

    {
        int namelen = (int)(sep - args);
        if (namelen <= 0 || namelen >= (int)sizeof(namebuf) - 1) {
            protocol_send_raw("ERR|FONTINFO|Invalid font name");
            return;
        }
        memcpy(namebuf, args, namelen);
        namebuf[namelen] = '\0';
    }

    size = (UWORD)strtoul(sep + 1, NULL, 10);
    if (size == 0) {
        protocol_send_raw("ERR|FONTINFO|Invalid size");
        return;
    }

    /* Set up TextAttr and open the font */
    ta.ta_Name = (STRPTR)namebuf;
    ta.ta_YSize = size;
    ta.ta_Style = FS_NORMAL;
    ta.ta_Flags = 0;

    tf = OpenDiskFont(&ta);
    if (!tf) {
        /* Fall back to graphics.library OpenFont for ROM fonts */
        tf = OpenFont(&ta);
    }
    if (!tf) {
        protocol_send_raw("ERR|FONTINFO|Font not found");
        return;
    }

    sprintf(linebuf, "FONTINFO|%s|%ld|%ld|%ld|%ld|%ld|%ld",
            namebuf,
            (long)size,
            (long)tf->tf_YSize,
            (long)tf->tf_XSize,
            (long)tf->tf_Style,
            (long)tf->tf_Flags,
            (long)tf->tf_Baseline);
    protocol_send_raw(linebuf);

    CloseFont(tf);
}
