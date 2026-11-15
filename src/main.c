#include <stdint.h>

/* Non-zero initial value would live in .data; the zero-initialised one in .bss. */
volatile uint64_t bss_probe;
volatile uint64_t data_probe = 0x1234;

int main(void)
{
    bss_probe = data_probe + 1;   /* expect 0x1235 */
    for (;;)
        __asm__ volatile("wfe");
}
