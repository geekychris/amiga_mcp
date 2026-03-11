/*
 * pool_tracker.c - Memory pool operation tracker
 *
 * Patches exec.library pool functions (CreatePool, DeletePool,
 * AllocPooled, FreePooled) via SetFunction() to track pool usage.
 * Uses the same "unhook, call, re-hook" pattern as snoop.c.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <proto/exec.h>

#include <string.h>
#include <stdio.h>

#include "bridge_internal.h"

extern struct ExecBase *SysBase;

/* ---- Pool tracking data ---- */

#define MAX_POOLS 32

struct PoolEntry {
    BOOL  active;
    APTR  pool;          /* Pool handle returned by CreatePool */
    ULONG puddleSize;
    ULONG threshSize;
    ULONG allocCount;
    ULONG totalAlloc;
};

static struct PoolEntry g_pools[MAX_POOLS];
static BOOL g_active = FALSE;

/* ---- Original function pointers ---- */

static APTR orig_CreatePool  = NULL;
static APTR orig_DeletePool  = NULL;
static APTR orig_AllocPooled = NULL;
static APTR orig_FreePooled  = NULL;

/* ---- LVO offsets ---- */

#define LVO_CreatePool  (-696)
#define LVO_DeletePool  (-702)
#define LVO_AllocPooled (-708)
#define LVO_FreePooled  (-714)

/* ---- Helpers ---- */

static struct PoolEntry *find_pool(APTR pool)
{
    int i;
    for (i = 0; i < MAX_POOLS; i++) {
        if (g_pools[i].active && g_pools[i].pool == pool) {
            return &g_pools[i];
        }
    }
    return NULL;
}

static struct PoolEntry *alloc_pool_entry(void)
{
    int i;
    for (i = 0; i < MAX_POOLS; i++) {
        if (!g_pools[i].active) {
            return &g_pools[i];
        }
    }
    return NULL;
}

/* ---- Patch functions ---- */

/*
 * CreatePool patch (LVO -696)
 * Register args: D0=memFlags, D1=puddleSize, D2=threshSize, A6=SysBase
 */
static APTR patch_CreatePool(void)
{
    register ULONG d0_flags   __asm("d0");
    register ULONG d1_puddle  __asm("d1");
    register ULONG d2_thresh  __asm("d2");
    ULONG flags = d0_flags;
    ULONG puddle = d1_puddle;
    ULONG thresh = d2_thresh;
    APTR result;

    /* Unhook, call original, re-hook */
    Disable();
    SetFunction((struct Library *)SysBase, LVO_CreatePool,
                (ULONG (*)())orig_CreatePool);
    Enable();

    result = CreatePool(flags, puddle, thresh);

    Disable();
    orig_CreatePool = (APTR)SetFunction((struct Library *)SysBase,
                       LVO_CreatePool, (ULONG (*)())patch_CreatePool);
    Enable();

    /* Record the new pool */
    if (result) {
        struct PoolEntry *pe;
        Forbid();
        pe = alloc_pool_entry();
        if (pe) {
            pe->active = TRUE;
            pe->pool = result;
            pe->puddleSize = puddle;
            pe->threshSize = thresh;
            pe->allocCount = 0;
            pe->totalAlloc = 0;
        }
        Permit();
    }

    return result;
}

/*
 * DeletePool patch (LVO -702)
 * Register args: A0=poolHeader, A6=SysBase
 */
static void patch_DeletePool(void)
{
    register APTR a0_pool __asm("a0");
    APTR pool = a0_pool;

    /* Remove from tracking before deletion */
    {
        struct PoolEntry *pe;
        Forbid();
        pe = find_pool(pool);
        if (pe) {
            pe->active = FALSE;
            pe->pool = NULL;
        }
        Permit();
    }

    Disable();
    SetFunction((struct Library *)SysBase, LVO_DeletePool,
                (ULONG (*)())orig_DeletePool);
    Enable();

    DeletePool(pool);

    Disable();
    orig_DeletePool = (APTR)SetFunction((struct Library *)SysBase,
                       LVO_DeletePool, (ULONG (*)())patch_DeletePool);
    Enable();
}

/*
 * AllocPooled patch (LVO -708)
 * Register args: A0=poolHeader, D0=memSize, A6=SysBase
 */
static APTR patch_AllocPooled(void)
{
    register APTR  a0_pool __asm("a0");
    register ULONG d0_size __asm("d0");
    APTR pool = a0_pool;
    ULONG size = d0_size;
    APTR result;

    Disable();
    SetFunction((struct Library *)SysBase, LVO_AllocPooled,
                (ULONG (*)())orig_AllocPooled);
    Enable();

    result = AllocPooled(pool, size);

    Disable();
    orig_AllocPooled = (APTR)SetFunction((struct Library *)SysBase,
                        LVO_AllocPooled, (ULONG (*)())patch_AllocPooled);
    Enable();

    if (result) {
        struct PoolEntry *pe;
        Forbid();
        pe = find_pool(pool);
        if (pe) {
            pe->allocCount++;
            pe->totalAlloc += size;
        }
        Permit();
    }

    return result;
}

/*
 * FreePooled patch (LVO -714)
 * Register args: A0=poolHeader, A1=memory, D0=memSize, A6=SysBase
 */
static void patch_FreePooled(void)
{
    register APTR  a0_pool __asm("a0");
    register APTR  a1_mem  __asm("a1");
    register ULONG d0_size __asm("d0");
    APTR pool = a0_pool;
    APTR mem = a1_mem;
    ULONG size = d0_size;

    /* Update tracking before freeing */
    {
        struct PoolEntry *pe;
        Forbid();
        pe = find_pool(pool);
        if (pe) {
            if (pe->allocCount > 0) pe->allocCount--;
            if (pe->totalAlloc >= size)
                pe->totalAlloc -= size;
            else
                pe->totalAlloc = 0;
        }
        Permit();
    }

    Disable();
    SetFunction((struct Library *)SysBase, LVO_FreePooled,
                (ULONG (*)())orig_FreePooled);
    Enable();

    FreePooled(pool, mem, size);

    Disable();
    orig_FreePooled = (APTR)SetFunction((struct Library *)SysBase,
                       LVO_FreePooled, (ULONG (*)())patch_FreePooled);
    Enable();
}

/* ---- Public API ---- */

/*
 * Initialize the pool tracker.
 */
void pool_init(void)
{
    memset(g_pools, 0, sizeof(g_pools));
    g_active = FALSE;
    printf("PoolTracker: initialized\n");
}

/*
 * Cleanup the pool tracker - ensure patches are removed.
 */
void pool_cleanup(void)
{
    if (g_active) {
        pool_handle_stop();
    }
    printf("PoolTracker: cleaned up\n");
}

/*
 * Start tracking pool operations (install patches).
 * Command: POOLSTART
 *
 * Response: OK|POOL|tracking started
 */
void pool_handle_start(void)
{
    if (g_active) {
        protocol_send_raw("OK|POOL|already tracking");
        return;
    }

    /* Clear tracking data */
    memset(g_pools, 0, sizeof(g_pools));

    /* Install patches */
    Disable();
    orig_CreatePool = (APTR)SetFunction((struct Library *)SysBase,
                       LVO_CreatePool, (ULONG (*)())patch_CreatePool);
    orig_DeletePool = (APTR)SetFunction((struct Library *)SysBase,
                       LVO_DeletePool, (ULONG (*)())patch_DeletePool);
    orig_AllocPooled = (APTR)SetFunction((struct Library *)SysBase,
                        LVO_AllocPooled, (ULONG (*)())patch_AllocPooled);
    orig_FreePooled = (APTR)SetFunction((struct Library *)SysBase,
                       LVO_FreePooled, (ULONG (*)())patch_FreePooled);
    Enable();

    g_active = TRUE;

    protocol_send_raw("OK|POOL|tracking started");
    ui_add_log("PoolTracker: started");
    printf("PoolTracker: 4 functions patched\n");
}

/*
 * Stop tracking pool operations (remove patches).
 * Command: POOLSTOP
 *
 * Response: OK|POOL|tracking stopped
 */
void pool_handle_stop(void)
{
    if (!g_active) {
        protocol_send_raw("OK|POOL|not tracking");
        return;
    }

    g_active = FALSE;

    /* Restore original function vectors */
    Disable();

    if (orig_CreatePool)
        SetFunction((struct Library *)SysBase, LVO_CreatePool,
                    (ULONG (*)())orig_CreatePool);
    if (orig_DeletePool)
        SetFunction((struct Library *)SysBase, LVO_DeletePool,
                    (ULONG (*)())orig_DeletePool);
    if (orig_AllocPooled)
        SetFunction((struct Library *)SysBase, LVO_AllocPooled,
                    (ULONG (*)())orig_AllocPooled);
    if (orig_FreePooled)
        SetFunction((struct Library *)SysBase, LVO_FreePooled,
                    (ULONG (*)())orig_FreePooled);

    Enable();

    orig_CreatePool  = NULL;
    orig_DeletePool  = NULL;
    orig_AllocPooled = NULL;
    orig_FreePooled  = NULL;

    protocol_send_raw("OK|POOL|tracking stopped");
    ui_add_log("PoolTracker: stopped");
    printf("PoolTracker: all patches removed\n");
}

/*
 * List all tracked pools.
 * Command: POOLS
 *
 * Response: POOLS|count|addr1:puddleSize:threshSize:allocCount:totalAlloc|...
 */
void pool_handle_list(void)
{
    static char linebuf[BRIDGE_MAX_LINE];
    int pos, i, count;

    /* Count active pools */
    count = 0;
    Forbid();
    for (i = 0; i < MAX_POOLS; i++) {
        if (g_pools[i].active) count++;
    }

    sprintf(linebuf, "POOLS|%ld", (long)count);
    pos = strlen(linebuf);

    for (i = 0; i < MAX_POOLS && pos < BRIDGE_MAX_LINE - 60; i++) {
        if (g_pools[i].active) {
            char entry[56];
            int elen;

            sprintf(entry, "%08lx:%lu:%lu:%lu:%lu",
                    (unsigned long)g_pools[i].pool,
                    (unsigned long)g_pools[i].puddleSize,
                    (unsigned long)g_pools[i].threshSize,
                    (unsigned long)g_pools[i].allocCount,
                    (unsigned long)g_pools[i].totalAlloc);
            elen = strlen(entry);

            if (pos + 1 + elen >= BRIDGE_MAX_LINE - 2) break;
            linebuf[pos++] = '|';
            memcpy(&linebuf[pos], entry, elen);
            pos += elen;
        }
    }
    Permit();

    linebuf[pos] = '\0';
    protocol_send_raw(linebuf);
}
