/*
 * Defender - High score persistence
 */
#include <proto/dos.h>
#include <string.h>
#include "game.h"
#include "score.h"

void score_init(ScoreTable *st)
{
    WORD i;
    st->count = 0;
    for (i = 0; i < MAX_HISCORES; i++) {
        memset(st->entries[i].name, 0, HISCORE_NAMELEN);
        st->entries[i].score = 0;
    }

    /* Default entries */
    strcpy(st->entries[0].name, "ACE");   st->entries[0].score = 10000;
    strcpy(st->entries[1].name, "PILOT"); st->entries[1].score = 8000;
    strcpy(st->entries[2].name, "HERO");  st->entries[2].score = 6000;
    strcpy(st->entries[3].name, "CADET"); st->entries[3].score = 4000;
    strcpy(st->entries[4].name, "NOOB");  st->entries[4].score = 2000;
    st->count = 5;
}

void score_load(ScoreTable *st)
{
    BPTR fh;

    fh = Open((STRPTR)HISCORE_FILE, MODE_OLDFILE);
    if (!fh) return;

    {
        char all[512];
        LONG len = Read(fh, all, 511);
        char *p;
        Close(fh);
        if (len <= 0) return;
        all[len] = 0;

        st->count = 0;
        p = all;
        while (*p && st->count < MAX_HISCORES) {
            HiScoreEntry *e = &st->entries[st->count];
            /* char *namestart = p; */
            WORD ni = 0;
            LONG sc = 0;

            /* Read name (up to space) */
            while (*p && *p != ' ' && *p != '\n' && ni < HISCORE_NAMELEN - 1) {
                e->name[ni++] = *p++;
            }
            e->name[ni] = 0;

            /* Skip space */
            while (*p == ' ') p++;

            /* Read score */
            while (*p >= '0' && *p <= '9') {
                sc = sc * 10 + (*p - '0');
                p++;
            }
            e->score = sc;

            /* Skip to next line */
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;

            if (ni > 0 && sc > 0)
                st->count++;
        }
    }
}

void score_save(ScoreTable *st)
{
    BPTR fh;
    WORD i;
    char buf[64];

    fh = Open((STRPTR)HISCORE_FILE, MODE_NEWFILE);
    if (!fh) return;

    for (i = 0; i < st->count; i++) {
        LONG slen;
        sprintf(buf, "%s %ld\n", st->entries[i].name, (long)st->entries[i].score);
        slen = strlen(buf);
        Write(fh, buf, slen);
    }

    Close(fh);
}

WORD score_qualifies(ScoreTable *st, LONG score)
{
    WORD i;
    if (score <= 0) return -1;

    if (st->count < MAX_HISCORES)
        return st->count; /* auto-qualify if table not full */

    for (i = 0; i < st->count; i++) {
        if (score > st->entries[i].score)
            return i;
    }
    return -1;
}

void score_insert(ScoreTable *st, WORD rank, const char *name, LONG score)
{
    WORD i;

    if (rank < 0 || rank > MAX_HISCORES) return;

    /* Shift entries down */
    for (i = MAX_HISCORES - 1; i > rank; i--)
        st->entries[i] = st->entries[i - 1];

    /* Insert */
    strncpy(st->entries[rank].name, name, HISCORE_NAMELEN - 1);
    st->entries[rank].name[HISCORE_NAMELEN - 1] = 0;
    st->entries[rank].score = score;

    if (st->count < MAX_HISCORES)
        st->count++;
}
