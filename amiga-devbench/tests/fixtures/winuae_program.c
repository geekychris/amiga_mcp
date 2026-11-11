/* SPDX-License-Identifier: MIT */
/* Debug-info fixture for tests/test_winuae_program.py; see README.md. */
#include <proto/exec.h>
#include <proto/dos.h>

struct point { long x; short y; char tag[6]; };

struct point origin = { 3, 4, "orig" };
long counter;
__chip unsigned short chip_words[3] = { 1, 2, 3 };

static long scale(long v, long k)
{
    long r = v * k;
    return r + counter;
}

long walk(struct point *p, int n)
{
    long total = 0;
    int i;
    for (i = 0; i < n; i++) {
        total += scale(p->x, i) + p->y;
        counter++;
    }
    return total;
}

int main(void)
{
    struct point local = origin;
    long r = walk(&local, 5);
    Printf("result %ld %ld\n", r, (long)chip_words[1]);
    return 0;
}
