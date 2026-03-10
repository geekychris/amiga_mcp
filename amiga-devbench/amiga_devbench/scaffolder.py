"""Project scaffolding for new Amiga apps."""

from __future__ import annotations

import os
import re
import logging

logger = logging.getLogger(__name__)

# ─── Makefile Template ───

MAKEFILE_TEMPLATE = """\
CC = m68k-amigaos-gcc
CFLAGS = -noixemul -O2 -m68020 -Wall -I../../amiga-bridge/include
LDFLAGS = -noixemul -L../../amiga-bridge -lbridge -lamiga
TARGET = {name}

all: $(TARGET)

$(TARGET): main.o
\t$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
\t$(CC) $(CFLAGS) -c -o $@ $<

clean:
\trm -f *.o $(TARGET)
"""

# ─── Window Template ───

WINDOW_TEMPLATE = """\
#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

#include <stdio.h>
#include <string.h>

#include "bridge_client.h"

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;

#define VERSION "1.0"

static LONG loop_count = 0;
static LONG running = 1;
static LONG bridge_ok = 0;

/* Hook example: called remotely via bridge */
static int hook_status(const char *args, char *result, int bufsize)
{{
    sprintf(result, "loop_count=%ld running=%ld", (long)loop_count, (long)running);
    return 0;
}}

int main(void)
{{
    struct Window *win;
    struct IntuiMessage *msg;
    ULONG class;
    ULONG signals;
    LONG hb_counter = 0;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 36);
    if (!IntuitionBase) return 1;

    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 36);
    if (!GfxBase) {{
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }}

    printf("{name} v%s\\n", VERSION);

    /* Connect to AmigaBridge daemon */
    if (ab_init("{name}") != 0) {{
        printf("  Bridge: NOT FOUND (is amiga-bridge running?)\\n");
        bridge_ok = 0;
    }} else {{
        printf("  Bridge: CONNECTED\\n");
        bridge_ok = 1;
    }}

    AB_I("{name} v%s starting", VERSION);

    /* Register variables for remote inspection */
    ab_register_var("loop_count", AB_TYPE_I32, &loop_count);
    ab_register_var("running", AB_TYPE_I32, &running);

    /* Register hooks for remote control */
    ab_register_hook("status", "Get current status", hook_status);

    /* Open window */
    win = OpenWindowTags(NULL,
        WA_Left, 50,
        WA_Top, 50,
        WA_Width, 320,
        WA_Height, 150,
        WA_Title, (ULONG)(bridge_ok ?
            "{name} v1.0 [Bridge: OK]" :
            "{name} v1.0 [Bridge: OFF]"),
        WA_CloseGadget, TRUE,
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW,
        WA_Activate, TRUE,
        TAG_DONE);

    if (!win) {{
        AB_E("Failed to open window");
        ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }}

    AB_I("Window opened successfully");

    /* Main loop */
    while (running) {{
        /* Check CTRL-C */
        signals = SetSignal(0L, 0L);
        if (signals & SIGBREAKF_CTRL_C) {{
            AB_I("CTRL-C received");
            running = 0;
            break;
        }}

        /* Check for window messages */
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {{
            class = msg->Class;
            ReplyMsg((struct Message *)msg);

            if (class == IDCMP_CLOSEWINDOW) {{
                AB_I("Close window requested");
                running = 0;
            }}
        }}

        loop_count++;

        /* Log every 500 iterations */
        if ((loop_count % 500) == 0) {{
            AB_D("Loop iteration %ld", (long)loop_count);
        }}

        /* Heartbeat every 500 iterations */
        if ((++hb_counter % 500) == 0) {{
            ab_heartbeat();
        }}

        /* Poll for commands from bridge daemon */
        ab_poll();

        /* TODO: Add your rendering/logic here */

        Delay(5);  /* ~10fps */
    }}

    AB_I("{name} shutting down");

    CloseWindow(win);
    ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    printf("{name} finished.\\n");
    return 0;
}}
"""

# ─── Screen Template ───

SCREEN_TEMPLATE = """\
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

#include <stdio.h>
#include <string.h>

#include "bridge_client.h"

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;

#define VERSION "1.0"
#define SCR_WIDTH 320
#define SCR_HEIGHT 256
#define SCR_DEPTH 5

static LONG frame_count = 0;
static LONG running = 1;
static LONG bridge_ok = 0;

/* Hook example */
static int hook_status(const char *args, char *result, int bufsize)
{{
    sprintf(result, "frame=%ld running=%ld", (long)frame_count, (long)running);
    return 0;
}}

int main(void)
{{
    struct Screen *scr;
    struct Window *win;
    struct RastPort *rp;
    struct ViewPort *vp;
    ULONG signals;
    LONG hb_counter = 0;
    LONG i;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 36);
    if (!IntuitionBase) return 1;

    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 36);
    if (!GfxBase) {{
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }}

    printf("{name} v%s\\n", VERSION);

    /* Connect to AmigaBridge daemon */
    if (ab_init("{name}") != 0) {{
        printf("  Bridge: NOT FOUND (is amiga-bridge running?)\\n");
        bridge_ok = 0;
    }} else {{
        printf("  Bridge: CONNECTED\\n");
        bridge_ok = 1;
    }}

    AB_I("{name} v%s starting", VERSION);

    /* Register variables */
    ab_register_var("frame_count", AB_TYPE_I32, &frame_count);
    ab_register_var("running", AB_TYPE_I32, &running);

    /* Register hooks */
    ab_register_hook("status", "Get current status", hook_status);

    /* Open custom screen */
    scr = OpenScreenTags(NULL,
        SA_Width, SCR_WIDTH,
        SA_Height, SCR_HEIGHT,
        SA_Depth, SCR_DEPTH,
        SA_Title, (ULONG)"{name}",
        SA_ShowTitle, FALSE,
        SA_Type, CUSTOMSCREEN,
        TAG_DONE);

    if (!scr) {{
        AB_E("Failed to open screen");
        ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }}

    /* Open backdrop window on the screen */
    win = OpenWindowTags(NULL,
        WA_CustomScreen, (ULONG)scr,
        WA_Left, 0,
        WA_Top, 0,
        WA_Width, SCR_WIDTH,
        WA_Height, SCR_HEIGHT,
        WA_Borderless, TRUE,
        WA_Backdrop, TRUE,
        WA_Activate, TRUE,
        WA_RMBTrap, TRUE,
        WA_IDCMP, IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS,
        TAG_DONE);

    if (!win) {{
        AB_E("Failed to open window");
        CloseScreen(scr);
        ab_cleanup();
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 1;
    }}

    rp = &scr->RastPort;
    vp = &scr->ViewPort;

    /* Set up palette - example gradient */
    for (i = 0; i < (1L << SCR_DEPTH); i++) {{
        SetRGB4(vp, i,
            (UBYTE)((i * 15L) / 31L),
            (UBYTE)((i * 10L) / 31L),
            (UBYTE)((i * 5L) / 31L));
    }}

    AB_I("Screen opened: %ldx%ldx%ld", (long)SCR_WIDTH, (long)SCR_HEIGHT, (long)SCR_DEPTH);

    /* Main loop */
    while (running) {{
        struct IntuiMessage *msg;

        /* Check CTRL-C */
        signals = SetSignal(0L, 0L);
        if (signals & SIGBREAKF_CTRL_C) {{
            AB_I("CTRL-C received");
            running = 0;
            break;
        }}

        /* Check for input */
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {{
            ULONG class = msg->Class;
            UWORD code = msg->Code;
            ReplyMsg((struct Message *)msg);

            if (class == IDCMP_RAWKEY && code == 0x45) {{
                /* ESC key */
                AB_I("ESC pressed, exiting");
                running = 0;
            }}
        }}

        frame_count++;

        /* TODO: Add your rendering here */
        /* Example: clear and draw something */
        SetAPen(rp, (UBYTE)(frame_count & 0x1F));
        RectFill(rp, 0, 0, SCR_WIDTH - 1, SCR_HEIGHT - 1);

        /* Heartbeat every 500 frames */
        if ((++hb_counter % 500) == 0) {{
            ab_heartbeat();
        }}

        /* Poll bridge */
        ab_poll();

        WaitTOF();  /* Sync to vertical blank */
    }}

    AB_I("{name} shutting down");

    CloseWindow(win);
    CloseScreen(scr);
    ab_cleanup();
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    printf("{name} finished.\\n");
    return 0;
}}
"""

# ─── Headless Template ───

HEADLESS_TEMPLATE = """\
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

#include "bridge_client.h"

#define VERSION "1.0"

static LONG loop_count = 0;
static LONG running = 1;
static LONG bridge_ok = 0;
static char status_msg[128] = "Ready";

/* Hook example: called remotely via bridge */
static int hook_status(const char *args, char *result, int bufsize)
{{
    sprintf(result, "loop_count=%ld status=%s", (long)loop_count, status_msg);
    return 0;
}}

/* Hook example: perform an action */
static int hook_action(const char *args, char *result, int bufsize)
{{
    if (!args || !args[0]) {{
        sprintf(result, "Usage: provide an argument");
        return 1;
    }}
    /* TODO: Do something with args */
    sprintf(result, "Action performed: %s", args);
    AB_I("Action: %s", args);
    return 0;
}}

int main(void)
{{
    ULONG signals;
    LONG hb_counter = 0;

    printf("{name} v%s\\n", VERSION);

    /* Connect to AmigaBridge daemon */
    if (ab_init("{name}") != 0) {{
        printf("  Bridge: NOT FOUND (is amiga-bridge running?)\\n");
        bridge_ok = 0;
    }} else {{
        printf("  Bridge: CONNECTED\\n");
        bridge_ok = 1;
    }}

    AB_I("{name} v%s starting", VERSION);

    /* Register variables for remote inspection */
    ab_register_var("loop_count", AB_TYPE_I32, &loop_count);
    ab_register_var("running", AB_TYPE_I32, &running);
    ab_register_var("status_msg", AB_TYPE_STR, status_msg);

    /* Register hooks for remote control */
    ab_register_hook("status", "Get current status", hook_status);
    ab_register_hook("action", "Perform an action (arg: description)", hook_action);

    sprintf(status_msg, "Running");

    /* Main loop - no GUI, just poll and respond */
    while (running) {{
        /* Check CTRL-C */
        signals = SetSignal(0L, 0L);
        if (signals & SIGBREAKF_CTRL_C) {{
            AB_I("CTRL-C received");
            running = 0;
            break;
        }}

        loop_count++;

        /* Log every 1000 iterations */
        if ((loop_count % 1000) == 0) {{
            AB_D("Loop iteration %ld", (long)loop_count);
        }}

        /* Heartbeat every 500 iterations */
        if ((++hb_counter % 500) == 0) {{
            ab_heartbeat();
        }}

        /* Poll for commands from bridge daemon */
        ab_poll();

        /* TODO: Add your background processing here */

        Delay(10);  /* ~5fps polling rate */
    }}

    AB_I("{name} shutting down");
    sprintf(status_msg, "Stopped");

    ab_cleanup();

    printf("{name} finished.\\n");
    return 0;
}}
"""

TEMPLATES = {
    "window": WINDOW_TEMPLATE,
    "screen": SCREEN_TEMPLATE,
    "headless": HEADLESS_TEMPLATE,
}


def create_project(project_root: str, name: str, template_type: str = "window") -> str:
    """Create a new Amiga project with boilerplate code.

    Args:
        project_root: Root of the amiga_mcp project (contains examples/, Makefile).
        name: Project name (used as directory name and binary name).
        template_type: One of 'window', 'screen', 'headless'.

    Returns:
        Summary string of what was created.
    """
    if template_type not in TEMPLATES:
        return f"Unknown template type '{template_type}'. Choose from: {', '.join(TEMPLATES.keys())}"

    # Validate name: alphanumeric + underscores only
    if not re.match(r'^[a-zA-Z][a-zA-Z0-9_]*$', name):
        return f"Invalid project name '{name}'. Use letters, digits, and underscores (must start with a letter)."

    project_dir = os.path.join(project_root, "examples", name)

    if os.path.exists(project_dir):
        return f"Project directory already exists: {project_dir}"

    os.makedirs(project_dir, exist_ok=True)

    # Write Makefile
    makefile_path = os.path.join(project_dir, "Makefile")
    with open(makefile_path, "w") as f:
        f.write(MAKEFILE_TEMPLATE.format(name=name))

    # Write main.c
    main_c_path = os.path.join(project_dir, "main.c")
    template = TEMPLATES[template_type]
    with open(main_c_path, "w") as f:
        f.write(template.format(name=name))

    # Update top-level Makefile
    top_makefile = os.path.join(project_root, "Makefile")
    makefile_updated = _update_top_makefile(top_makefile, name)

    summary_parts = [
        f"Created project '{name}' ({template_type} template):",
        f"  {makefile_path}",
        f"  {main_c_path}",
    ]
    if makefile_updated:
        summary_parts.append(f"  Updated {top_makefile} (added to examples and clean targets)")
    else:
        summary_parts.append(f"  NOTE: Could not update {top_makefile} - add manually")

    summary_parts.append("")
    summary_parts.append(f"Build with: make -C examples/{name}")
    summary_parts.append(f"  Or: make examples  (builds all)")

    return "\n".join(summary_parts)


def _update_top_makefile(makefile_path: str, name: str) -> bool:
    """Add the new project to the top-level Makefile's examples and clean targets.

    Returns True if successfully updated, False otherwise.
    """
    if not os.path.exists(makefile_path):
        return False

    with open(makefile_path, "r") as f:
        content = f.read()

    entry = f"examples/{name}"

    # Check if already present
    if entry in content:
        return True

    # Find the last $(DOCKER_RUN) make -C examples/... line in the examples target
    # and add our new line after it
    examples_pattern = r'((\t\$\(DOCKER_RUN\) make -C examples/[a-zA-Z0-9_]+\n)+)'
    match = re.search(examples_pattern, content)
    if not match:
        return False

    examples_block = match.group(0)
    new_line = f"\t$(DOCKER_RUN) make -C examples/{name}\n"
    new_examples_block = examples_block + new_line
    content = content.replace(examples_block, new_examples_block, 1)

    # Find the last clean examples line and add our new line after it
    clean_pattern = r'((\t\$\(DOCKER_RUN\) make -C examples/[a-zA-Z0-9_]+ clean\n)+)'
    match = re.search(clean_pattern, content)
    if not match:
        return False

    clean_block = match.group(0)
    new_clean_line = f"\t$(DOCKER_RUN) make -C examples/{name} clean\n"
    new_clean_block = clean_block + new_clean_line
    content = content.replace(clean_block, new_clean_block, 1)

    with open(makefile_path, "w") as f:
        f.write(content)

    return True
