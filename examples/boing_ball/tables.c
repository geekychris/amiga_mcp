#include "tables.h"

/* Pre-computed sin/cos tables in 8.8 fixed point.
 * Generated from standard math, 256 entries = 360 degrees.
 * sin_table[i] = sin(i * 2*PI / 256) * 256
 */

WORD sin_table[TABLE_SIZE];
WORD cos_table[TABLE_SIZE];

/* Standard sine values for first quadrant (0..63) in 8.8 fixed point */
static const WORD sine_q1[65] = {
      0,   6,  12,  18,  25,  31,  37,  43,
     49,  56,  62,  68,  74,  80,  86,  91,
     97, 103, 109, 114, 120, 125, 130, 136,
    141, 146, 151, 156, 160, 165, 170, 174,
    178, 182, 186, 190, 194, 197, 201, 204,
    207, 210, 213, 216, 218, 221, 223, 225,
    227, 229, 230, 232, 233, 234, 235, 236,
    237, 238, 238, 239, 239, 239, 240, 240,
    240
};

void tables_init(void)
{
    WORD i;

    /* Build full sine table from first quadrant */
    for (i = 0; i <= 64; i++) {
        sin_table[i] = sine_q1[i];
        sin_table[128 - i] = sine_q1[i];
        sin_table[128 + i] = -sine_q1[i];
        if (i > 0)
            sin_table[256 - i] = -sine_q1[i];
    }

    /* Cosine = sine shifted by 90 degrees (64 entries) */
    for (i = 0; i < TABLE_SIZE; i++) {
        cos_table[i] = sin_table[(i + 64) & (TABLE_SIZE - 1)];
    }
}

UWORD isqrt(UWORD val)
{
    UWORD result = 0;
    UWORD bit = 1 << 14; /* Start with highest power of 4 <= 65535 */

    while (bit > val)
        bit >>= 2;

    while (bit != 0) {
        if (val >= result + bit) {
            val -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}
