#ifndef PRINTF_H
#define PRINTF_H

int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif