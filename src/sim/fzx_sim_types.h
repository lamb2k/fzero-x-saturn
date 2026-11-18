/* Fixed-width integer types for the sim lane's shared headers. The decomp's
 * include/libc/stdint.h (first on the closure's include path) defines no
 * intN_t types and an unsigned INT32_MIN; inside the closure take the types
 * from PR/ultratypes.h instead. Elsewhere this is <stdint.h>. MIT. */
#ifndef FZX_SIM_TYPES_H
#define FZX_SIM_TYPES_H
#include <stdint.h>
#include <stdbool.h>
#if defined(LIBC_STDINT_H) && !defined(INT32_C)
typedef s8 int8_t;
typedef u8 uint8_t;
typedef s16 int16_t;
typedef u16 uint16_t;
typedef s32 int32_t;
typedef u32 uint32_t;
typedef s64 int64_t;
typedef u64 uint64_t;
#undef INT32_MIN
#undef INT32_MAX
#undef UINT32_MAX
#undef UINT64_MAX
#define INT32_MAX ((int32_t)0x7FFFFFFF)
#define INT32_MIN (-INT32_MAX - 1)
#define UINT32_MAX ((uint32_t)0xFFFFFFFFu)
#define UINT64_MAX ((uint64_t)0xFFFFFFFFFFFFFFFFull)
#define INT32_C(x) (x)
#define UINT32_C(x) (x##u)
#define INT64_C(x) (x##ll)
#define UINT64_C(x) (x##ull)
#endif
#endif
