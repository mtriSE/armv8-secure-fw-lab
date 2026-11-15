#include "uart.h"
#include "printf.h"

#include <stdarg.h>
#include <stdint.h>

/* Print v in the given base (2..16). Returns the number of chars printed. */
static int print_num(uint64_t v, unsigned base)
{
    static const char digits[] = "0123456789abcdef";
    char buf[20]; /* UINT64_MAX has 20 decimal digits */
    int n = 0;

    if (base < 2 || base > 16)
        return 0;

    /* do-while so that v == 0 still prints "0". Digits come out
     * least-significant first, so they are stored reversed. */
    do
    {
        buf[n++] = digits[v % base];
        v /= base;
    } while (v != 0);

    for (int i = n - 1; i >= 0; i--)
        uart_putc(buf[i]);
    return n;
}

static int print_str(const char *s)
{
    int n = 0;

    if (s == 0)
        s = "(null)";
    while (*s)
    {
        uart_putc(*s++);
        n++;
    }
    return n;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    int count = 0;

    va_start(ap, fmt);
    for (; *fmt; fmt++)
    {
        if (*fmt != '%')
        {
            uart_putc(*fmt);
            count++;
            continue;
        }

        fmt++; /* skip '%' */
        int is_long = 0;
        if (*fmt == 'l')
        { /* 'l' = 64-bit argument */
            is_long = 1;
            fmt++;
        }

        switch (*fmt)
        {
        case 'c':
            /* char is promoted to int when passed through '...' */
            uart_putc((char)va_arg(ap, int));
            count++;
            break;
        case 's':
            count += print_str(va_arg(ap, const char *));
            break;
        case 'd':
        {
            int64_t v = is_long ? va_arg(ap, int64_t) : va_arg(ap, int);
            uint64_t u = (uint64_t)v;
            if (v < 0)
            {
                uart_putc('-');
                count++;
                /* Negate in unsigned: correct even for INT64_MIN,
                 * where -v would overflow. */
                u = 0 - u;
            }
            count += print_num(u, 10);
            break;
        }
        case 'u':
            count += print_num(is_long ? va_arg(ap, uint64_t)
                                       : va_arg(ap, unsigned int),
                               10);
            break;
        case 'x':
            count += print_num(is_long ? va_arg(ap, uint64_t)
                                       : va_arg(ap, unsigned int),
                               16);
            break;
        case 'p':
            count += print_str("0x");
            count += print_num((uintptr_t)va_arg(ap, void *), 16);
            break;
        case '%':
            uart_putc('%');
            count++;
            break;
        case '\0': /* format ends with a lone '%' */
            goto out;
        default: /* unknown: print it as-is */
            uart_putc('%');
            uart_putc(*fmt);
            count += 2;
            break;
        }
    }
out:
    va_end(ap);
    return count;
}