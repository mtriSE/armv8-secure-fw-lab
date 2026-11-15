#include "uart.h"
#include "mmio.h"

#include <stddef.h>

#define UART0_BASE 0x09000000UL

/* Offsets */
#define UART_DR 0x00
#define UART_FR 0x18
#define UART_IBRD 0x24
#define UART_FBRD 0x28
#define UART_LCR_H 0x2C
#define UART_CR 0x30
#define UART_IMSC 0x38

/* Bit fields */
#define FR_BUSY (1u << 3)
#define FR_TXFF (1u << 5)
#define LCR_H_FEN (1u << 4)
#define LCR_H_WLEN_8 (3u << 5) /* WLEN = 0b11 → 8 bits */
#define CR_UARTEN (1u << 0)
#define CR_TXE (1u << 8)
#define CR_RXE (1u << 9)

/* 24 MHz UARTCLK (DT apb-pclk), 115200 baud: 24e6/(16*115200) = 13.02 */
#define UART_IBRD_115200 13U
#define UART_FBRD_115200 1U   /* round(0.0208 * 64) */

void uart_init(void)
{
    mmio_write32(UART0_BASE + UART_CR, 0U);

    while (mmio_read32(UART0_BASE + UART_FR) & FR_BUSY)
        ;

    mmio_write32(UART0_BASE + UART_LCR_H, mmio_read32(UART0_BASE + UART_LCR_H) & ~(LCR_H_FEN));

    mmio_write32(UART0_BASE + UART_IBRD, UART_IBRD_115200);
    mmio_write32(UART0_BASE + UART_FBRD, UART_FBRD_115200);

    mmio_write32(UART0_BASE + UART_LCR_H, LCR_H_WLEN_8 | LCR_H_FEN);

    mmio_write32(UART0_BASE + UART_IMSC, 0U);
    mmio_write32(UART0_BASE + UART_CR, CR_UARTEN | CR_TXE | CR_RXE);
}

void uart_putc(char c)
{
    /* Terminals need CR before LF to return to column 0. */
    if (c == '\n')
        uart_putc('\r');

    /* Wait while the TX FIFO is full. mmio_read32 is volatile,
     * so UARTFR is re-read on every iteration. */
    while (mmio_read32(UART0_BASE + UART_FR) & FR_TXFF)
        ;

    /* Cast through uint8_t so chars > 127 are not sign-extended. */
    mmio_write32(UART0_BASE + UART_DR, (uint8_t)c);
}

void uart_puts(const char *s)
{
    if (s == NULL)
        return;

    /* '\n' -> "\r\n" is handled once, in uart_putc. */
    while (*s)
        uart_putc(*s++);
}