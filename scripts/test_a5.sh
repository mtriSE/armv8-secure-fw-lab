#!/bin/bash
# A5 regression check: Non-secure EL1 MMU with hand-written level-1 table.
# Device vs Normal memory (unaligned access), an alias mapping, a
# translation fault on an unmapped VA.
source "$(dirname "$0")/lib.sh"

out=$(run_firmware)
H='0x[0-9a-f]+'

fail=0
expect_lines A5 E "$out" \
    "A5: before MMU: unaligned load fault EC=0x25 DFSC=0x21 OK" \
    "A5: MMU on: SCTLR_EL1=$H TCR_EL1=0x803520 MAIR_EL1=0xff00" \
    "A5: after MMU: unaligned load val=0x807060504030201 OK" \
    "A5: alias 0xc[0-9a-f]+ -> 0x4[0-9a-f]+ reads 0x5a5a OK" \
    "A5: unmapped 0x80000000: fault EC=0x25 DFSC=0x5 FAR=0x80000000 OK" \
    "A5: done" \
    "EL3: Non-secure EL1 done \(failures=0\)" || fail=1
# The MMU must really be on (SCTLR_EL1.M, bit 0).
sctlr=$(grep -oE 'SCTLR_EL1=0x[0-9a-f]+' <<<"$out" | head -1 | cut -d= -f2)
if [ -z "$sctlr" ] || (( (sctlr & 1) == 0 )); then
    echo "A5: FAIL SCTLR_EL1.M not set (${sctlr:-none})"
    fail=1
fi
expect_no_panic A5 "$out" || fail=1
finish A5 $fail "$out"
