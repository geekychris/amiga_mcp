/*
 * Close all custom screens named "Frank the Frog"
 * Run this to clean up orphan screens from crashed game instances.
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <string.h>
#include <stdio.h>

struct IntuitionBase *IntuitionBase = NULL;

int main(void)
{
    struct Screen *scr;
    int closed = 0;
    ULONG lock;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 36);
    if (!IntuitionBase) return 1;

    /* Keep closing until none left */
    for (;;) {
        struct Screen *target = NULL;

        lock = LockIBase(0);
        scr = IntuitionBase->FirstScreen;
        while (scr) {
            if (scr->Title && strcmp((char *)scr->Title, "Frank the Frog") == 0) {
                target = scr;
                break;
            }
            scr = scr->NextScreen;
        }
        UnlockIBase(lock);

        if (!target) break;

        /* Close any windows on this screen first */
        {
            struct Window *w;
            lock = LockIBase(0);
            w = target->FirstWindow;
            UnlockIBase(lock);
            while (w) {
                struct Window *wnext;
                lock = LockIBase(0);
                wnext = w->NextWindow;
                UnlockIBase(lock);
                CloseWindow(w);
                w = wnext;
            }
        }

        CloseScreen(target);
        closed++;
    }

    if (closed > 0) {
        char buf[64];
        sprintf(buf, "Closed %ld orphan screen(s)\n", (long)closed);
        Write(Output(), buf, strlen(buf));
    } else {
        Write(Output(), "No orphan screens found\n", 24);
    }

    CloseLibrary((struct Library *)IntuitionBase);
    return 0;
}
