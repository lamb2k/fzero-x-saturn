/* Freestanding declarations for src/wasm_support.c; web build only. */
#ifndef FZX_WASM_STRING_H
#define FZX_WASM_STRING_H
#include <stddef.h>
void *memcpy(void *destination, const void *source, size_t count);
void *memmove(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);
#endif
