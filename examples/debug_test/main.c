/*
 * debug_test - Simple program for testing the remote debugger.
 *
 * Runs a counter loop with function calls, ideal for:
 * - Setting breakpoints in the loop body
 * - Single-stepping through arithmetic
 * - Step-over on function calls
 * - Inspecting registers and variables
 * - Backtrace through nested calls
 */

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>

#include "bridge_client.h"

/* Globals visible to debugger */
static LONG counter = 0;
static LONG total = 0;
static LONG running = 1;

/* Simple function for stepping into */
static LONG add_values(LONG a, LONG b)
{
    LONG result = a + b;
    return result;
}

/* Nested call for backtrace testing */
static LONG multiply(LONG a, LONG b)
{
    LONG sum = 0;
    LONG i;

    for (i = 0; i < b; i++) {
        sum = add_values(sum, a);
    }
    return sum;
}

/* Main computation each tick */
static void update_tick(void)
{
    counter++;

    /* Interesting arithmetic for register inspection */
    if ((counter % 10) == 0) {
        total = add_values(total, counter);
        AB_I("counter=%ld total=%ld", (long)counter, (long)total);
    }

    if ((counter % 50) == 0) {
        LONG product = multiply(counter, 3);
        AB_I("multiply(%ld, 3) = %ld", (long)counter, (long)product);
    }

    if (counter >= 500) {
        AB_I("Reached 500, stopping");
        running = 0;
    }
}

int main(void)
{
    printf("debug_test starting\n");

    if (ab_init("debug_test") != 0) {
        printf("  Bridge: NOT FOUND (running standalone)\n");
    } else {
        printf("  Bridge: CONNECTED\n");
    }

    /* Register variables for inspection */
    ab_register_var("counter", AB_TYPE_I32, &counter);
    ab_register_var("total", AB_TYPE_I32, &total);
    ab_register_var("running", AB_TYPE_I32, &running);

    AB_I("debug_test ready - set breakpoints and step through!");

    while (running) {
        update_tick();
        ab_poll();
        Delay(10);  /* ~5 ticks/sec, slow enough to catch with debugger */
    }

    AB_I("debug_test finished: counter=%ld total=%ld", (long)counter, (long)total);
    ab_cleanup();
    printf("debug_test done\n");

    return 0;
}
