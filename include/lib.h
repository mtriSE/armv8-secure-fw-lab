#ifndef LIB_H
#define LIB_H
#include <stddef.h>
/* Implemented in src/lib.c (no libc; <string.h> is not a freestanding header). */
void *memset(void *dst, int c, size_t n);
void *memcpy(void *restrict dst, const void *restrict src, size_t n);
#endif
