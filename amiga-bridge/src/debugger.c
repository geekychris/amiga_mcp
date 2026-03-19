/*
 * debugger.c - Remote debugger for AmigaBridge (Phase 2)
 *
 * Software breakpoints via TRAP #15 + cooperative IPC pause.
 *
 * How it works:
 * 1. BPSET patches target code with TRAP #15 (0x4E4F)
 * 2. When target hits TRAP, our handler checks ThisTask == dbg_target
 * 3. If match: restore original instruction, send IPC PAUSE to self,
 *    enter ab_poll() pause loop (cooperative)
 * 4. If not our target: call original TRAP #15 handler
 *
 * The TRAP #15 handler runs in supervisor mode but immediately
 * transitions to user mode by modifying the exception return.
 * The actual pause happens cooperatively via IPC in user mode.
 *
 * Key safety rules:
 * - amiga.lib sprintf returns char*, NOT int — use strlen() after
 * - TypeOfMem() before any address access
 * - CacheClearU() after code patching (68020+ I-cache)
 * - Supervisor() for VBR access
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <exec/tasks.h>
#include <exec/nodes.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_internal.h"

extern struct ExecBase *SysBase;

/* ---- Constants ---- */

#define MAX_BREAKPOINTS    32
#define MAX_BT_FRAMES      16
#define TRAP15_OPCODE       0x4E4F

/* ---- Data structures ---- */

struct Breakpoint {
    ULONG address;
    UWORD original;
    BOOL  enabled;     /* Currently patched in code? */
    BOOL  temporary;
    BOOL  used;
};

/* ---- Static state ---- */

static struct Breakpoint bp_table[MAX_BREAKPOINTS];
static struct Task *dbg_target = NULL;
static char dbg_target_name[64];
static ULONG dbg_saved_regs[18];  /* D0-D7, A0-A7, PC, SR */
static BOOL dbg_stopped = FALSE;
static BOOL dbg_attached = FALSE;
static int  dbg_bp_count = 0;
static ULONG dbg_code_base = 0;  /* Code segment load address */

/* Exception vector management */
static APTR orig_trap15 = NULL;
static APTR orig_trace = NULL;
static BOOL trap_installed = FALSE;
static volatile BOOL dbg_trap_hit = FALSE;
static volatile ULONG dbg_trap_pc = 0;
static volatile ULONG dbg_resume_pc = 0;  /* Where to resume after pause stub */

/* Step mode: 0=none, 1=step-past-BP (internal), 2=single-step (user) */
static volatile WORD dbg_step_mode = 0;
#define STEP_PAST_BP  1
#define STEP_SINGLE   2

/* Static buffer for responses */
static char dbg_buf[BRIDGE_MAX_LINE];

/* ---- Forward declarations ---- */
static void dbg_send_stop(const char *reason);
static void dbg_send_regs(void);
static int  dbg_find_bp_by_addr(ULONG addr);
static int  dbg_find_free_bp(void);
static void dbg_install_trap(void);
static void dbg_remove_trap(void);
static void dbg_patch_bp(int idx);
static void dbg_unpatch_bp(int idx);
static void dbg_unpatch_all(void);
static void dbg_repatch_all(void);
void dbg_repatch_all_from_trace(void);
void dbg_signal_self_pause(void);

/* ---- VBR access via Supervisor() ---- */

static ULONG vbr_value;

/*
 * Read VBR in supervisor mode. Written in pure asm because
 * Supervisor() expects the function to end with RTE, not RTS.
 * A C function would use RTS and crash.
 */
static void __attribute__((used)) read_vbr_asm(void);
__asm(
    ".text\n"
    ".even\n"
    ".global _read_vbr_asm\n"
    "_read_vbr_asm:\n"
    "    movec   %vbr, %d0\n"
    "    move.l  %d0, _vbr_value\n"
    "    rte\n"
);

static ULONG get_vbr(void)
{
    vbr_value = 0;
    Supervisor((ULONG (*)())read_vbr_asm);
    return vbr_value;
}

/* ---- TRAP #15 handler ---- */

/*
 * Our TRAP #15 replacement. Called in supervisor mode.
 * Exception frame on 68020+:
 *   [SP+0]  SR (WORD)
 *   [SP+2]  PC (LONG) - points AFTER the TRAP instruction
 *   [SP+6]  Format/Vector (WORD)
 *
 * Strategy: check if ThisTask is our target. If so, adjust PC
 * back to the breakpoint address, restore the original instruction,
 * set a flag, and RTE. The target will then execute the original
 * instruction. The daemon's main loop polls the flag and sends
 * the IPC PAUSE message.
 *
 * This is simpler and safer than trying to do IPC from supervisor
 * mode or modifying the exception return to a suspend stub.
 */
static void __attribute__((used)) trap15_handler(void);
static void __attribute__((used)) trace_handler(void);

/* Pure asm handler - must be careful not to use any stack-allocated C vars */
__asm(
    ".text\n"
    ".even\n"
    ".global _trap15_handler\n"
    "_trap15_handler:\n"
    /* First: check if this is our target (use only a0, preserve stack) */
    "    move.l  _SysBase, %a0\n"
    "    move.l  276(%a0), %a0\n"          /* a0 = SysBase->ThisTask */
    "    cmp.l   _dbg_target, %a0\n"
    "    bne     .Lnot_ours\n"

    /* === It's our target. Save ALL registers for inspection === */
    "    movem.l %d0-%d7/%a0-%a6, _dbg_saved_regs\n"  /* D0-D7 at +0, A0-A6 at +32 */
    "    move.l  %usp, %a0\n"
    "    move.l  %a0, _dbg_saved_regs+60\n"  /* A7 = USP */
    /* Save SR from exception frame */
    "    move.w  (%sp), %d0\n"
    "    and.l   #0xFFFF, %d0\n"
    "    move.l  %d0, _dbg_saved_regs+68\n"  /* regs[17] = SR */

    /* Get the TRAP address (PC in frame points after TRAP) */
    "    move.l  2(%sp), %a1\n"
    "    subq.l  #2, %a1\n"               /* a1 = TRAP address */
    "    move.l  %a1, _dbg_saved_regs+64\n"  /* regs[16] = PC */

    /* Find the BP entry and restore original instruction */
    "    move.l  %a1, %d0\n"
    "    lea     _bp_table, %a0\n"
    /* struct: 0:addr(4) 4:orig(2) 6:enabled(2) 8:temp(2) 10:used(2) = 12 bytes */
    "    moveq   #31, %d1\n"
    ".Lfind_bp:\n"
    "    tst.w   10(%a0)\n"
    "    beq     .Lnext_bp\n"
    "    cmp.l   (%a0), %d0\n"
    "    bne     .Lnext_bp\n"
    "    move.w  4(%a0), (%a1)\n"          /* restore original instruction */
    "    clr.w   6(%a0)\n"                /* enabled = FALSE */
    "    bra     .Lfound_bp\n"
    ".Lnext_bp:\n"
    "    lea     12(%a0), %a0\n"
    "    dbf     %d1, .Lfind_bp\n"
    ".Lfound_bp:\n"
    /* Save PC and set flag (first hit wins) */
    "    tst.w   _dbg_trap_hit\n"
    "    bne     .Lskip_save\n"
    "    move.l  %a1, _dbg_trap_pc\n"
    "    move.w  #1, _dbg_trap_hit\n"
    ".Lskip_save:\n"

    /* Flush caches */
    "    move.l  _SysBase, %a6\n"
    "    jsr     -0x27c(%a6)\n"            /* CacheClearU */

    /* Set T1 (Trace) bit in saved SR so the Trace exception fires
     * after executing the restored original instruction.
     * The Trace handler will repatch all BPs. */
    "    move.w  (%sp), %d0\n"
    "    or.w    #0x8000, %d0\n"           /* Set T1 bit (bit 15) */
    "    move.w  %d0, (%sp)\n"

    /* Set step mode to STEP_PAST_BP */
    "    move.w  #1, _dbg_step_mode\n"     /* STEP_PAST_BP */

    /* RTE to the original instruction (with T1 set).
     * Target executes ONE instruction, then Trace fires. */
    "    move.l  %a1, 2(%sp)\n"
    "    rte\n"

    /* Not our target */
    ".Lnot_ours:\n"
    "    move.l  _orig_trap15, -(%sp)\n"
    "    rts\n"
);

/*
 * Trace exception handler (vector 9).
 * Fires after one instruction when T1 bit is set in SR.
 */
__asm(
    ".text\n"
    ".even\n"
    ".global _trace_handler\n"
    "_trace_handler:\n"
    "    move.l  _SysBase, %a0\n"
    "    move.l  276(%a0), %a0\n"
    "    cmp.l   _dbg_target, %a0\n"
    "    bne     .Ltrace_not_ours\n"

    /* Clear T1 bit in saved SR */
    "    move.w  (%sp), %d0\n"
    "    and.w   #0x7FFF, %d0\n"
    "    move.w  %d0, (%sp)\n"

    /* Check step mode */
    "    move.w  _dbg_step_mode, %d0\n"
    "    cmp.w   #1, %d0\n"
    "    beq     .Ltrace_repatch\n"
    "    cmp.w   #2, %d0\n"
    "    beq     .Ltrace_single\n"
    "    rte\n"

    ".Ltrace_repatch:\n"
    /* Step-past-BP: repatch all BPs, save resume PC, RTE to pause stub.
     * The stub does Wait(CTRL_F) then jumps to resume_pc.
     * This stops the target IMMEDIATELY after stepping past one BP,
     * so the next BP in execution order will fire on continue. */
    "    clr.w   _dbg_step_mode\n"
    "    move.l  2(%sp), _dbg_resume_pc\n"
    "    jsr     _dbg_repatch_all_from_trace\n"
    "    move.l  #_dbg_pause_stub, 2(%sp)\n"
    "    rte\n"

    ".Ltrace_single:\n"
    /* Single-step: save new PC, save regs, repatch, pause in stub */
    "    clr.w   _dbg_step_mode\n"
    "    move.l  2(%sp), _dbg_trap_pc\n"
    "    move.l  2(%sp), _dbg_resume_pc\n"
    "    movem.l %d0-%d7/%a0-%a6, _dbg_saved_regs\n"
    "    move.l  %usp, %a0\n"
    "    move.l  %a0, _dbg_saved_regs+60\n"
    "    move.l  2(%sp), _dbg_saved_regs+64\n"
    "    move.w  (%sp), %d0\n"
    "    and.l   #0xFFFF, %d0\n"
    "    move.l  %d0, _dbg_saved_regs+68\n"
    "    move.w  #1, _dbg_trap_hit\n"
    "    jsr     _dbg_repatch_all_from_trace\n"
    "    rte\n"

    ".Ltrace_not_ours:\n"
    "    move.l  _orig_trace, -(%sp)\n"
    "    rts\n"
);

/* C function called from Trace handler to signal self with CTRL_E.
 * This runs on the TARGET task's context. After RTE, the target will
 * check CTRL_E in ab_poll() and immediately pause. This means the
 * target stops at the FIRST BP it hits, before reaching any others. */
void dbg_signal_self_pause(void)
{
    struct Task *me = SysBase->ThisTask;
    Signal(me, SIGBREAKF_CTRL_E);
}

/* C function called from Trace handler to repatch BPs */
void dbg_repatch_all_from_trace(void)
{
    int i;
    for (i = 0; i < MAX_BREAKPOINTS; i++) {
        if (bp_table[i].used && !bp_table[i].temporary) {
            *(UWORD *)bp_table[i].address = TRAP15_OPCODE;
            bp_table[i].enabled = TRUE;
        }
    }
    CacheClearU();
}

/*
 * Pause stub. The Trace handler RTEs here after stepping past a BP.
 * Runs in user mode on the target's stack. Calls Wait(CTRL_F) to
 * freeze the target, then jumps to dbg_resume_pc (the instruction
 * AFTER the BP, set by the Trace handler).
 */
static void __attribute__((used)) dbg_pause_stub(void);
__asm(
    ".text\n"
    ".even\n"
    ".global _dbg_pause_stub\n"
    "_dbg_pause_stub:\n"
    /* Wait for CTRL_F (resume signal from daemon) */
    "    move.l  #0x8000, %d0\n"           /* SIGBREAKF_CTRL_F = 1<<15 = 0x8000 */
    "    move.l  _SysBase, %a6\n"
    "    jsr     -0x13e(%a6)\n"            /* Wait() */
    /* Restore registers from the static array saved by the TRAP handler.
     * This is critical because the Trace handler trashed registers
     * during repatch before RTE'ing here. */
    "    movem.l _dbg_saved_regs, %d0-%d7/%a0-%a6\n"
    /* Jump to the instruction AFTER the BP */
    "    move.l  _dbg_resume_pc, -(%sp)\n"
    "    rts\n"
);

/* ---- Vector installation ---- */

static void dbg_install_trap(void)
{
    ULONG vbr;
    APTR *vectors;

    if (trap_installed) return;

    vbr = get_vbr();
    vectors = (APTR *)vbr;

    Disable();
    orig_trap15 = vectors[47];  /* TRAP #15 = vector 47 */
    vectors[47] = (APTR)trap15_handler;
    orig_trace = vectors[9];   /* Trace = vector 9 */
    vectors[9] = (APTR)trace_handler;
    Enable();

    CacheClearU();
    trap_installed = TRUE;
    printf("  Debugger: TRAP #15 + Trace handlers installed (VBR=%08lx)\n",
           (unsigned long)vbr);
}

static void dbg_remove_trap(void)
{
    ULONG vbr;
    APTR *vectors;

    if (!trap_installed) return;

    vbr = get_vbr();
    vectors = (APTR *)vbr;

    Disable();
    vectors[47] = orig_trap15;
    orig_trap15 = NULL;
    vectors[9] = orig_trace;
    orig_trace = NULL;
    Enable();

    CacheClearU();
    trap_installed = FALSE;
    printf("  Debugger: TRAP #15 handler removed\n");
}

/* ---- Breakpoint patching ---- */

/*
 * CacheClearE is needed on 68040/060 to flush the data cache
 * before invalidating the instruction cache. CacheClearU alone
 * may not flush modified data cache lines on these CPUs.
 */
static void dbg_flush_cache(APTR addr, ULONG len)
{
    /* Use CacheClearU() for maximum compatibility across all 68k CPUs.
     * CacheClearE with specific flags may not work on all FS-UAE configs. */
    (void)addr;
    (void)len;
    CacheClearU();
}

static void dbg_patch_bp(int idx)
{
    ULONG addr;
    if (idx < 0 || idx >= MAX_BREAKPOINTS) return;
    if (!bp_table[idx].used || bp_table[idx].enabled) return;

    addr = bp_table[idx].address;
    Disable();
    *(UWORD *)addr = TRAP15_OPCODE;
    Enable();
    dbg_flush_cache((APTR)addr, 2);
    bp_table[idx].enabled = TRUE;
}

static void dbg_unpatch_bp(int idx)
{
    ULONG addr;
    if (idx < 0 || idx >= MAX_BREAKPOINTS) return;
    if (!bp_table[idx].used || !bp_table[idx].enabled) return;

    addr = bp_table[idx].address;
    Disable();
    *(UWORD *)addr = bp_table[idx].original;
    Enable();
    dbg_flush_cache((APTR)addr, 2);
    bp_table[idx].enabled = FALSE;
}

static void dbg_unpatch_all(void)
{
    int i;
    Disable();
    for (i = 0; i < MAX_BREAKPOINTS; i++) {
        if (bp_table[i].used && bp_table[i].enabled) {
            *(UWORD *)bp_table[i].address = bp_table[i].original;
            bp_table[i].enabled = FALSE;
        }
    }
    Enable();
    /* Flush all modified addresses */
    for (i = 0; i < MAX_BREAKPOINTS; i++) {
        if (bp_table[i].used) {
            dbg_flush_cache((APTR)bp_table[i].address, 2);
        }
    }
}

static void dbg_repatch_all(void)
{
    int i;
    Disable();
    for (i = 0; i < MAX_BREAKPOINTS; i++) {
        if (bp_table[i].used && !bp_table[i].temporary) {
            *(UWORD *)bp_table[i].address = TRAP15_OPCODE;
            bp_table[i].enabled = TRUE;
        }
    }
    Enable();
    CacheClearU();
}

/* ---- IPC helpers ---- */



/* ---- Task finding ---- */

static struct Task *dbg_find_task(const char *name_or_addr)
{
    struct Task *task = NULL;
    char *endptr;
    ULONG addr;

    if (name_or_addr[0] == '0' && (name_or_addr[1] == 'x' || name_or_addr[1] == 'X')) {
        addr = strtoul(name_or_addr + 2, NULL, 16);
        if (TypeOfMem((APTR)addr)) {
            return (struct Task *)addr;
        }
    }

    addr = strtoul(name_or_addr, &endptr, 16);
    if (*endptr == '\0' && addr > 0x1000 && TypeOfMem((APTR)addr)) {
        return (struct Task *)addr;
    }

    Forbid();
    task = FindTask((CONST_STRPTR)name_or_addr);
    Permit();
    return task;
}

/* ---- Response formatting ---- */
/* NOTE: amiga.lib sprintf returns char*, NOT int. Always use strlen(). */

static void dbg_send_stop(const char *reason)
{
    int pos, i;

    sprintf(dbg_buf, "DBGSTOP|%s|%08lx|%04lx|",
            reason,
            (unsigned long)dbg_saved_regs[16],
            (unsigned long)(dbg_saved_regs[17] & 0xFFFF));
    pos = strlen(dbg_buf);

    for (i = 0; i < 8; i++) {
        if (i > 0) dbg_buf[pos++] = ':';
        sprintf(dbg_buf + pos, "%08lx", (unsigned long)dbg_saved_regs[i]);
        pos += 8;
    }
    dbg_buf[pos++] = '|';

    for (i = 0; i < 8; i++) {
        if (i > 0) dbg_buf[pos++] = ':';
        sprintf(dbg_buf + pos, "%08lx", (unsigned long)dbg_saved_regs[8 + i]);
        pos += 8;
    }

    dbg_buf[pos] = '\0';
    protocol_send_raw(dbg_buf);
}

static void dbg_send_regs(void)
{
    int pos, i;

    sprintf(dbg_buf, "DBGREGS|");
    pos = strlen(dbg_buf);

    for (i = 0; i < 8; i++) {
        if (i > 0) dbg_buf[pos++] = ':';
        sprintf(dbg_buf + pos, "%08lx", (unsigned long)dbg_saved_regs[i]);
        pos += 8;
    }
    dbg_buf[pos++] = '|';

    for (i = 0; i < 8; i++) {
        if (i > 0) dbg_buf[pos++] = ':';
        sprintf(dbg_buf + pos, "%08lx", (unsigned long)dbg_saved_regs[8 + i]);
        pos += 8;
    }

    sprintf(dbg_buf + pos, "|%08lx|%04lx",
            (unsigned long)dbg_saved_regs[16],
            (unsigned long)(dbg_saved_regs[17] & 0xFFFF));

    protocol_send_raw(dbg_buf);
}

static void dbg_send_backtrace(void)
{
    ULONG fp, pc;
    int depth = 0, pos, depth_pos;

    sprintf(dbg_buf, "DBGBT|");
    pos = strlen(dbg_buf);

    pc = dbg_saved_regs[16];
    fp = dbg_saved_regs[13];

    depth_pos = pos;
    pos += 2;
    dbg_buf[pos++] = '|';

    sprintf(dbg_buf + pos, "%08lx", (unsigned long)pc);
    pos += 8;
    depth = 1;

    while (depth < MAX_BT_FRAMES && fp != 0) {
        if (!TypeOfMem((APTR)fp) || !TypeOfMem((APTR)(fp + 4)))
            break;
        pc = *(ULONG *)(fp + 4);
        if (pc == 0 || !TypeOfMem((APTR)pc))
            break;
        dbg_buf[pos++] = '|';
        sprintf(dbg_buf + pos, "%08lx", (unsigned long)pc);
        pos += 8;
        depth++;
        fp = *(ULONG *)fp;
    }

    dbg_buf[pos] = '\0';
    sprintf(dbg_buf + depth_pos, "%2ld", (long)depth);
    dbg_buf[depth_pos + 2] = '|';

    protocol_send_raw(dbg_buf);
}

/* ---- Breakpoint management ---- */

static int dbg_find_bp_by_addr(ULONG addr)
{
    int i;
    for (i = 0; i < MAX_BREAKPOINTS; i++) {
        if (bp_table[i].used && bp_table[i].address == addr)
            return i;
    }
    return -1;
}

static int dbg_find_free_bp(void)
{
    int i;
    for (i = 0; i < MAX_BREAKPOINTS; i++) {
        if (!bp_table[i].used)
            return i;
    }
    return -1;
}

/* ---- Code segment base address ---- */

/*
 * Get the code segment base address of a Process.
 * AmigaOS loads programs as relocatable hunks. pr_SegList is a
 * BPTR (BCPL pointer, >>2) to the first segment. The actual code
 * starts 4 bytes into the segment (after the next-segment pointer).
 *
 * This base address must be added to symbol offsets from nm/objdump
 * to get absolute addresses for breakpoints.
 */
static ULONG dbg_get_seglist_base(struct Task *task)
{
    struct Process *proc;
    struct CommandLineInterface *cli;
    BPTR seglist_bptr;
    ULONG *seg;
    ULONG code_base;

    if (!task) return 0;
    if (task->tc_Node.ln_Type != NT_PROCESS) return 0;

    proc = (struct Process *)task;

    /* For CLI-launched programs, the code segments are in
     * cli_Module of the CLI structure, not in pr_SegList.
     * pr_SegList for CLI processes contains the shell's own segments. */
    if (proc->pr_CLI) {
        cli = (struct CommandLineInterface *)BADDR(proc->pr_CLI);
        if (cli && TypeOfMem((APTR)cli)) {
            seglist_bptr = cli->cli_Module;
            printf("  Debugger: CLI program, cli_Module BPTR=%08lx\n",
                   (unsigned long)seglist_bptr);
        } else {
            seglist_bptr = proc->pr_SegList;
            printf("  Debugger: CLI struct invalid, using pr_SegList\n");
        }
    } else {
        /* Workbench-launched: use pr_SegList directly */
        seglist_bptr = proc->pr_SegList;
        printf("  Debugger: WB program, pr_SegList BPTR=%08lx\n",
               (unsigned long)seglist_bptr);
    }

    if (!seglist_bptr) return 0;

    /* Convert BPTR to real pointer */
    seg = (ULONG *)BADDR(seglist_bptr);
    if (!seg || !TypeOfMem((APTR)seg)) return 0;

    /* Segment format:
     *   seg[0] = BPTR to next segment (0 = last)
     *   seg[1..] = actual code
     * Code base = &seg[1]
     */
    code_base = (ULONG)&seg[1];

    printf("  Debugger: seg=%08lx -> code=%08lx\n",
           (unsigned long)seg, (unsigned long)code_base);

    /* Verify: first word should be 0x23C8 (move.l a0,...) for our binaries */
    if (TypeOfMem((APTR)code_base)) {
        UWORD first_word = *(UWORD *)code_base;
        printf("  Debugger: first word at code base: %04lx %s\n",
               (unsigned long)first_word,
               (first_word == 0x23C8) ? "(OK - move.l a0)" : "(unexpected!)");
    }

    return code_base;
}

/* ---- Polling: called from main loop timer tick ---- */

/*
 * Called from main.c's timer tick (~200ms). Checks if the TRAP
 * handler set the trap_hit flag, and if so, sends the IPC PAUSE
 * to the target client (which is now executing the original
 * instruction and will hit ab_poll() shortly).
 */
void dbg_poll(void)
{
    if (!dbg_attached || !dbg_trap_hit) return;

    dbg_trap_hit = FALSE;
    dbg_stopped = TRUE;
    dbg_saved_regs[16] = dbg_trap_pc;

    /* Target is already paused in the stub's Wait(CTRL_F).
     * Just send DBGSTOP to the host. */
    dbg_send_stop("breakpoint");
    ui_add_log("DBG: breakpoint hit");
}

/* ---- Protocol command handlers ---- */

void dbg_handle_attach(const char *args)
{
    struct Task *task;

    if (!args || args[0] == '\0') {
        protocol_send_raw("ERR|DBGATTACH|needs task name or hex address");
        return;
    }

    if (dbg_attached) {
        protocol_send_raw("ERR|DBGATTACH|already attached, detach first");
        return;
    }

    task = dbg_find_task(args);
    if (!task) {
        sprintf(dbg_buf, "ERR|DBGATTACH|task not found: %s", args);
        protocol_send_raw(dbg_buf);
        return;
    }

    if (task == SysBase->ThisTask) {
        protocol_send_raw("ERR|DBGATTACH|cannot debug self");
        return;
    }

    dbg_target = task;
    strncpy(dbg_target_name,
            task->tc_Node.ln_Name ? task->tc_Node.ln_Name : "?", 63);
    dbg_target_name[63] = '\0';
    dbg_attached = TRUE;
    dbg_stopped = FALSE;
    dbg_trap_hit = FALSE;

    memset(bp_table, 0, sizeof(bp_table));
    dbg_bp_count = 0;

    /* Get code segment base address */
    dbg_code_base = dbg_get_seglist_base(dbg_target);
    printf("  Debugger: code base = %08lx\n", (unsigned long)dbg_code_base);

    /* Install TRAP #15 handler (deferred until first BPSET to reduce crash risk) */

    sprintf(dbg_buf, "DBGSTATE|1|0|%s|%08lx|0|BASE:%08lx",
            dbg_target_name, (unsigned long)dbg_code_base,
            (unsigned long)dbg_code_base);
    protocol_send_raw(dbg_buf);

    printf("  Debugger: attached to '%s' @ %08lx\n",
           dbg_target_name, (unsigned long)task);
    ui_add_log("DBG: attached");
}

void dbg_handle_detach(void)
{
    int i;
    struct ClientEntry *ce;

    if (!dbg_attached) {
        protocol_send_raw("ERR|DBGDETACH|not attached");
        return;
    }

    /* Unpatch all breakpoints first */
    dbg_unpatch_all();

    /* Resume if stopped */
    if (dbg_stopped && dbg_target) {
        /* Clear trap flag so dbg_poll doesn't re-send CTRL_E */
        dbg_trap_hit = FALSE;
        /* Send CTRL_F to wake target from Wait(CTRL_F) */
        Signal(dbg_target, SIGBREAKF_CTRL_F);
        Delay(5);
        /* Send again in case target re-paused from stale CTRL_E */
        Signal(dbg_target, SIGBREAKF_CTRL_F);
        Delay(5);
    }

    /* Remove TRAP handler */
    dbg_remove_trap();

    for (i = 0; i < MAX_BREAKPOINTS; i++) {
        bp_table[i].used = FALSE;
    }

    dbg_target = NULL;
    dbg_target_name[0] = '\0';
    dbg_attached = FALSE;
    dbg_stopped = FALSE;
    dbg_bp_count = 0;
    dbg_trap_hit = FALSE;

    protocol_send_raw("DBGDETACHED");
    printf("  Debugger: detached\n");
    ui_add_log("DBG: detached");
}

void dbg_handle_break(void)
{
    struct ClientEntry *ce;

    if (!dbg_attached) {
        protocol_send_raw("ERR|DBGBREAK|not attached");
        return;
    }

    if (dbg_stopped) {
        dbg_send_stop("break");
        return;
    }

    if (!dbg_target) {
        protocol_send_raw("ERR|DBGBREAK|no target");
        return;
    }

    /* Signal the target to pause at next ab_poll() */
    Signal(dbg_target, SIGBREAKF_CTRL_E);

    dbg_stopped = TRUE;
    dbg_send_stop("break");
    ui_add_log("DBG: break");
}

void dbg_handle_bpset(const char *args)
{
    ULONG addr;
    int idx;

    if (!dbg_attached) {
        protocol_send_raw("ERR|BPSET|not attached");
        return;
    }

    if (!args || args[0] == '\0') {
        protocol_send_raw("ERR|BPSET|needs address_hex");
        return;
    }

    addr = strtoul(args, NULL, 16);

    if (!TypeOfMem((APTR)addr)) {
        sprintf(dbg_buf, "ERR|BPSET|address %08lx not in RAM",
                (unsigned long)addr);
        protocol_send_raw(dbg_buf);
        return;
    }

    if (addr & 1) {
        protocol_send_raw("ERR|BPSET|address must be word-aligned");
        return;
    }

    idx = dbg_find_bp_by_addr(addr);
    if (idx >= 0) {
        sprintf(dbg_buf, "BPINFO|%ld|%08lx|1|%04lx",
                (long)idx,
                (unsigned long)bp_table[idx].address,
                (unsigned long)bp_table[idx].original);
        protocol_send_raw(dbg_buf);
        return;
    }

    idx = dbg_find_free_bp();
    if (idx < 0) {
        protocol_send_raw("ERR|BPSET|breakpoint table full");
        return;
    }

    bp_table[idx].address = addr;
    bp_table[idx].original = *(UWORD *)addr;
    bp_table[idx].temporary = FALSE;
    bp_table[idx].used = TRUE;
    bp_table[idx].enabled = FALSE;
    dbg_bp_count++;

    printf("  Debugger: BP #%ld at %08lx (orig=%04lx)\n",
           (long)idx, (unsigned long)addr,
           (unsigned long)bp_table[idx].original);

    /* Install TRAP handler if not yet done */
    if (!trap_installed) {
        dbg_install_trap();
    }

    /* Patch the instruction with TRAP #15 */
    dbg_patch_bp(idx);

    sprintf(dbg_buf, "BPINFO|%ld|%08lx|1|%04lx",
            (long)idx,
            (unsigned long)addr,
            (unsigned long)bp_table[idx].original);
    protocol_send_raw(dbg_buf);
}

void dbg_handle_bpclear(const char *args)
{
    int idx;
    ULONG addr;

    if (!dbg_attached) {
        protocol_send_raw("ERR|BPCLEAR|not attached");
        return;
    }

    if (!args || args[0] == '\0') {
        protocol_send_raw("ERR|BPCLEAR|needs id or address");
        return;
    }

    idx = (int)strtol(args, NULL, 10);
    if (idx >= 0 && idx < MAX_BREAKPOINTS && bp_table[idx].used) {
        dbg_unpatch_bp(idx);
        bp_table[idx].used = FALSE;
        dbg_bp_count--;
        sprintf(dbg_buf, "OK|BPCLEAR|%ld", (long)idx);
        protocol_send_raw(dbg_buf);
        return;
    }

    addr = strtoul(args, NULL, 16);
    idx = dbg_find_bp_by_addr(addr);
    if (idx >= 0) {
        dbg_unpatch_bp(idx);
        bp_table[idx].used = FALSE;
        dbg_bp_count--;
        sprintf(dbg_buf, "OK|BPCLEAR|%ld", (long)idx);
        protocol_send_raw(dbg_buf);
        return;
    }

    protocol_send_raw("ERR|BPCLEAR|breakpoint not found");
}

void dbg_handle_bplist(void)
{
    int i, pos, first;

    if (!dbg_attached) {
        protocol_send_raw("ERR|BPLIST|not attached");
        return;
    }

    sprintf(dbg_buf, "BPLIST|%ld|", (long)dbg_bp_count);
    pos = strlen(dbg_buf);
    first = 1;

    for (i = 0; i < MAX_BREAKPOINTS; i++) {
        if (bp_table[i].used) {
            if (!first) dbg_buf[pos++] = ',';
            sprintf(dbg_buf + pos, "%ld:%08lx:%ld:%04lx",
                    (long)i,
                    (unsigned long)bp_table[i].address,
                    (long)bp_table[i].enabled,
                    (unsigned long)bp_table[i].original);
            pos += strlen(dbg_buf + pos);
            first = 0;
        }
    }

    dbg_buf[pos] = '\0';
    protocol_send_raw(dbg_buf);
}

void dbg_handle_step(void)
{
    if (!dbg_attached) {
        protocol_send_raw("ERR|DBGSTEP|not attached");
        return;
    }
    if (!dbg_stopped) {
        protocol_send_raw("ERR|DBGSTEP|target not stopped");
        return;
    }

    /* Set single-step mode. When the target resumes and hits a BP,
     * the TRAP handler sets T1. The Trace handler sees STEP_SINGLE
     * mode, saves the new PC, and lets dbg_poll() report it.
     * If the target is NOT at a BP, we need to set T1 directly...
     * but we can't modify the target's SR from here.
     * For now, step = continue to next BP. */
    dbg_step_mode = STEP_SINGLE;
    dbg_handle_continue();
}

void dbg_handle_next(void)
{
    if (!dbg_attached) {
        protocol_send_raw("ERR|DBGNEXT|not attached");
        return;
    }
    if (!dbg_stopped) {
        protocol_send_raw("ERR|DBGNEXT|target not stopped");
        return;
    }

    dbg_handle_continue();
}

void dbg_handle_continue(void)
{
    struct ClientEntry *ce;

    if (!dbg_attached) {
        protocol_send_raw("ERR|DBGCONT|not attached");
        return;
    }

    if (!dbg_stopped) {
        protocol_send_raw("ERR|DBGCONT|target not stopped");
        return;
    }

    /* BPs are already repatched by the Trace handler.
     * Signal the target to resume from the pause stub's Wait(CTRL_F).
     * The stub jumps to dbg_resume_pc (instruction AFTER the BP). */
    dbg_stopped = FALSE;
    protocol_send_raw("DBGRUNNING");
    ui_add_log("DBG: continue");

    if (dbg_target) {
        Signal(dbg_target, SIGBREAKF_CTRL_F);
    }
}

void dbg_handle_regs(void)
{
    if (!dbg_attached) {
        protocol_send_raw("ERR|DBGREGS|not attached");
        return;
    }
    dbg_send_regs();
}

void dbg_handle_setreg(const char *args)
{
    protocol_send_raw("ERR|DBGSETREG|not yet implemented");
}

void dbg_handle_backtrace(void)
{
    if (!dbg_attached) {
        protocol_send_raw("ERR|DBGBT|not attached");
        return;
    }
    dbg_send_backtrace();
}

void dbg_handle_clearall(void)
{
    int i;

    if (!dbg_attached) {
        protocol_send_raw("ERR|DBGCLEARALLBP|not attached");
        return;
    }

    /* Unpatch all breakpoints */
    dbg_unpatch_all();

    for (i = 0; i < MAX_BREAKPOINTS; i++) {
        bp_table[i].used = FALSE;
    }
    dbg_bp_count = 0;

    protocol_send_raw("OK|DBGCLEARALLBP|cleared");
}

void dbg_handle_status(void)
{
    sprintf(dbg_buf, "DBGSTATE|%ld|%ld|%s|%08lx|%ld",
            (long)dbg_attached,
            (long)dbg_stopped,
            dbg_attached ? dbg_target_name : "",
            dbg_stopped ? (unsigned long)dbg_saved_regs[16] : 0UL,
            (long)dbg_bp_count);
    protocol_send_raw(dbg_buf);
}

/* ---- Cleanup ---- */

void dbg_cleanup(void)
{
    if (dbg_attached) {
        dbg_unpatch_all();
        dbg_remove_trap();
        /* Resume if stopped */
        if (dbg_stopped && dbg_target) {
            Signal(dbg_target, SIGBREAKF_CTRL_F);
        }
        dbg_attached = FALSE;
        dbg_stopped = FALSE;
    }
}
