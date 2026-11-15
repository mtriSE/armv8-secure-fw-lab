#!/bin/bash
# A3 regression check: deliberate EL3 faults must be caught with the right
# EC/ISS, and the firmware must continue (no panic dump).
source "$(dirname "$0")/lib.sh"

out=$(run_firmware)
vbar=$(sym_addr build/firmware.elf el3_vectors)
H='0x[0-9a-f]+'

fail=0
# QEMU keeps the low bits of VBAR_EL3, so a misaligned table still "works"
# there; real hardware ignores bits [10:0]. Check the alignment explicitly.
if (( 0x$vbar % 0x800 != 0 )); then
    echo "A3: FAIL el3_vectors=0x$vbar is not 2KB aligned"
    fail=1
fi
expect_lines A3 E "$out" \
    "A3: VBAR_EL3=0x$vbar SCTLR_EL3=$H" \
    "A3: brk: taken=1 EC=0x3c ISS=0x42 ELR=$H FAR=$H OK" \
    "A3: udf: taken=1 EC=0x0 ISS=0x0 ELR=$H FAR=$H OK" \
    "A3: bad load: taken=1 EC=0x25 ISS=0x10 ELR=$H FAR=0xb000000 OK" \
    "A3: unaligned: taken=1 EC=0x25 ISS=0x21 ELR=$H FAR=$H OK" \
    "A3: done" || fail=1
expect_no_panic A3 "$out" || fail=1
finish A3 $fail "$out"
