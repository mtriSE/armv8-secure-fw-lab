#!/bin/bash
# A4 regression check: EL3 -> Secure EL1 -> (SMC DONE) -> Non-secure EL1,
# SMCCC calls from both worlds, Secure RAM visible only from Secure EL1.
source "$(dirname "$0")/lib.sh"

out=$(run_firmware)
vbar1=$(sym_addr build/payload.elf el1_vectors)

fail=0
if (( 0x$vbar1 % 0x800 != 0 )); then
    echo "A4: FAIL el1_vectors=0x$vbar1 is not 2KB aligned"
    fail=1
fi
lines=("A4: EL3: entering Secure EL1 payload at 0x40000000")
for w in S NS; do
    if [ $w = S ]; then world="0 (secure)"; else world="1 (non-secure)"; fi
    lines+=(
        "A4: [$w] EL1 payload: CurrentEL=1"
        "A4: [$w] smc GET_WORLD=$world"
        "A4: [$w] smc SMCCC_VERSION=0x10001 OK"
        "A4: [$w] smc ADD(1,2,3)=6 OK"
        "A4: [$w] smc unknown=0xffffffffffffffff OK"
    )
done
lines+=(
    "A4: [S] read secure RAM 0xe000000: val=0x1234 OK"
    "A4: EL3: Secure EL1 done (failures=0), entering Non-secure EL1"
    "A4: [NS] read secure RAM 0xe000000: fault EC=0x25 DFSC=0x10 FAR=0xe000000 OK"
    "EL3: Non-secure EL1 done (failures=0)"
)
expect_lines A4 F "$out" "${lines[@]}" || fail=1
expect_no_panic A4 "$out" || fail=1
finish A4 $fail "$out"
