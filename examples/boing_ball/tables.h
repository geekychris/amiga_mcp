#ifndef TABLES_H
#define TABLES_H

#include <exec/types.h>

/* Fixed-point 8.8 format */
#define FP_SHIFT 8
#define FP_ONE   (1 << FP_SHIFT)
#define FP_HALF  (FP_ONE >> 1)

#define FP_MUL(a, b) (((LONG)(a) * (LONG)(b)) >> FP_SHIFT)
#define FP_DIV(a, b) (((LONG)(a) << FP_SHIFT) / (LONG)(b))
#define INT_TO_FP(x) ((x) << FP_SHIFT)
#define FP_TO_INT(x) ((x) >> FP_SHIFT)

/* 256-entry tables covering 0..359 degrees */
#define TABLE_SIZE 256
#define DEG_TO_IDX(d) (((d) * TABLE_SIZE / 360) & (TABLE_SIZE - 1))

extern WORD sin_table[TABLE_SIZE];  /* Fixed 8.8 */
extern WORD cos_table[TABLE_SIZE];  /* Fixed 8.8 */

/* Fast integer square root (0..65535) */
UWORD isqrt(UWORD val);

void tables_init(void);

#endif
