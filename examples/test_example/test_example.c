/*
 * test_example.c - Example test program using bridge test harness
 */
#include <stdio.h>
#include <string.h>
#include "bridge_client.h"

/* Functions under test */
static int add(int a, int b) { return a + b; }
static int factorial(int n) {
    int result = 1, i;
    for (i = 2; i <= n; i++) result *= i;
    return result;
}

int main(void)
{
    if (!ab_init("test_example")) {
        printf("Failed to connect to AmigaBridge\n");
        return 1;
    }

    ab_test_begin("math_tests");

    AB_ASSERT(add(2, 3) == 5, "add_positive");
    AB_ASSERT(add(-1, 1) == 0, "add_negative");
    AB_ASSERT(add(0, 0) == 0, "add_zero");
    AB_ASSERT(factorial(0) == 1, "factorial_zero");
    AB_ASSERT(factorial(5) == 120, "factorial_five");
    AB_ASSERT(factorial(1) == 1, "factorial_one");

    ab_test_end();

    ab_cleanup();
    return 0;
}
