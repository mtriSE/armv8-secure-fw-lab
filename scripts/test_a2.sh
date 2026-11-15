#!/bin/bash
# A2 regression check: printf/UART output, matched line by line
# (pattern match per line, not a word-for-word diff).
source "$(dirname "$0")/lib.sh"

out=$(run_firmware)
# Address of bss_probe as placed by the linker, to cross-check %p.
bss_addr=$(sym_addr build/firmware.elf bss_probe)

fail=0
expect_lines A2 F "$out" \
    "A2: hello from EL3" \
    "A2: CurrentEL=3" \
    "A2: hex=0xdeadbeef" \
    "A2: hex64=0x123456789abcdef0" \
    "A2: dec=-42 zero=0 umax=4294967295" \
    "A2: dec64min=-9223372036854775808" \
    "A2: str=ok null=(null) chr=Z pct=%" \
    "A2: data_probe=0x1234 bss_probe=0x0 bss_probe@0x$bss_addr" \
    "A2: done" || fail=1
finish A2 $fail "$out"
