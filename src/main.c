#include "uart.h"
#include "printf.h"
#include "a3_tests.h"

#include <stdint.h>

/* Non-zero initial value would live in .data; the zero-initialised one in .bss. */
volatile uint64_t bss_probe;
volatile uint64_t data_probe = 0x1234;

/* CurrentEL holds the exception level in bits [3:2]. */
static unsigned int current_el(void)
{
    uint64_t v;

    __asm__ volatile("mrs %0, CurrentEL" : "=r"(v));
    return (v >> 2) & 3;
}

int main(void)
{
    /* volatile: hide the NULL from the compiler's format checks. */
    const char *volatile null_str = 0;

    uart_init();
    uart_puts("A2: hello from EL3\n");
    printf("A2: CurrentEL=%u\n", current_el());

    /* printf self-check: known values, matched by scripts/test_a2.sh. */
    printf("A2: hex=0x%x\n", 0xdeadbeefu);
    printf("A2: hex64=0x%lx\n", 0x123456789abcdef0ul);
    printf("A2: dec=%d zero=%d umax=%u\n", -42, 0, 0xffffffffu);
    printf("A2: dec64min=%ld\n", INT64_MIN);
    printf("A2: str=%s null=%s chr=%c pct=%%\n", "ok", null_str, 'Z');

    /* Cross-check A1 from inside the firmware: .data copied, .bss cleared. */
    printf("A2: data_probe=0x%lx bss_probe=0x%lx bss_probe@%p\n",
           data_probe, bss_probe, (void *)&bss_probe);

    bss_probe = data_probe + 1;   /* expect 0x1235 */
    printf("A2: done\n");

    a3_run_tests();
    for (;;)
        __asm__ volatile("wfe");
}
