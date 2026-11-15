/* A1 linker script.
 * With secure=on, QEMU `-bios` places the image in secure flash at 0 and the CPU
 * resets into EL3 there (like TF-A BL1). Flash is not writable by plain stores, so:
 *   code/rodata run from FLASH; .data is stored in FLASH (LMA) and copied to
 *   SECURE_RAM (VMA); .bss and the stack live in SECURE_RAM.
 * The Non-secure payload gets its own RAM region (used from A4).
 * Addresses verified via QEMU `info mtree` / dumpdtb (see docs/NOTES.md). */
ENTRY(_start)

MEMORY
{
    FLASH      (rx)  : ORIGIN = 0x00000000, LENGTH = 64M   /* secure flash0 */
    SECURE_RAM (rwx) : ORIGIN = 0x0e000000, LENGTH = 16M   /* virt.secure-ram */
    NS_RAM     (rwx) : ORIGIN = 0x40000000, LENGTH = 256M  /* mach-virt.ram, -m 256M */
}

STACK_SIZE = 0x4000;

SECTIONS
{
    .text : {
        KEEP(*(.text.boot))     /* _start must be at address 0 */
        *(.text .text.*)
    } > FLASH

    .rodata : ALIGN(8) { *(.rodata .rodata.*) } > FLASH

    __data_load = LOADADDR(.data);
    .data : ALIGN(8) {
        __data_start = .;
        *(.data .data.*)
        . = ALIGN(8);
        __data_end = .;
    } > SECURE_RAM AT > FLASH

    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.*) *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } > SECURE_RAM

    .stack (NOLOAD) : ALIGN(16) {
        . += STACK_SIZE;
        __stack_top = .;
    } > SECURE_RAM

    .ns_payload (NOLOAD) : { __ns_start = .; } > NS_RAM

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
