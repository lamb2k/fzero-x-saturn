/* Fast IEEE 754 binary32 arithmetic for the SH-2, bit-exact with SSE. MIT; see ../../LICENSE.
 * See fzx_float.c for the exact rules. */
#ifndef FZX_FLOAT_H
#define FZX_FLOAT_H

float fzx_fadd(float a, float b);
float fzx_fsub(float a, float b);
float fzx_fmul(float a, float b);
float fzx_fdiv(float a, float b);
int fzx_fcmp(float a, float b); /* -1, 0, 1, or 2 when unordered */
int fzx_ftoi(float a);          /* truncate; 0x80000000 when out of range or NaN */
float fzx_itof(int v);
float fzx_utof(unsigned int v);

#endif
