#ifndef SEMIHOST_H
#define SEMIHOST_H
#include <stdint.h>
/* Ask QEMU (started with -semihosting) to exit with the given status. */
void semihost_exit(uint32_t status) __attribute__((noreturn));
#endif
