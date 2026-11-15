/* EL1 payload, linked at the Non-secure RAM base (linker/link.ld __ns_start,
 * include/platform.h NS_RAM_BASE). Built as a raw binary and copied there by
 * EL3, so .data needs no separate load address: VMA == LMA. */
ENTRY(_start)

MEMORY
{
    NS_RAM (rwx) : ORIGIN = 0x40000000, LENGTH = 16M
}

STACK_SIZE = 0x4000;

SECTIONS
{
    .text : {
        KEEP(*(.text.boot))     /* _start at the load address */
        *(.text .text.*)
    } > NS_RAM

    .rodata : ALIGN(8) { *(.rodata .rodata.*) } > NS_RAM

    .data : ALIGN(8) { *(.data .data.*) } > NS_RAM

    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.*) *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } > NS_RAM

    .stack (NOLOAD) : ALIGN(16) {
        . += STACK_SIZE;
        __stack_top = .;
    } > NS_RAM

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
