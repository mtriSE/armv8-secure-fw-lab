/*
 * Minimal memset/memcpy for a freestanding build.
 *
 * Even with -ffreestanding, GCC may emit calls to memset/memcpy/memmove/memcmp
 * on its own, e.g. to zero-initialise a large local array or to copy a large
 * struct. With -nostdlib there is no libc to provide them, so we do.
 *
 * Plain byte loops on purpose: simple and correct for any alignment. Speed does
 * not matter here.
 */
#include "lib.h"

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;

    while (n--)
        *d++ = (unsigned char)c;
    return dst;
}

/* Regions must not overlap (memmove handles overlap). */
void *memcpy(void *restrict dst, const void *restrict src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;

    while (n--)
        *d++ = *s++;
    return dst;
}
