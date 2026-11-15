#!/bin/bash
# A6 regression check: EL3 assigns the NS EL1 physical timer (INTID 30) to
# Group 1 Non-secure; the NS payload receives 5 periodic timer IRQs at EL1.
source "$(dirname "$0")/lib.sh"

out=$(run_firmware)

fail=0
expect_lines A6 E "$out" \
    "A6: EL3: GICD_CTLR=0x37 GICR_WAKER=0x0 IGROUPR0=0x40000000 IGRPMODR0=0x0" \
    "A6: CNTFRQ_EL0=[0-9]+" \
    "A6: tick 1 intid=30" \
    "A6: tick 5 intid=30" \
    "A6: 5 timer IRQs in [0-9]+ ms OK" \
    "A6: done" \
    "EL3: Non-secure EL1 done \(failures=0\)" || fail=1
if grep -q '^A6: unexpected intid' <<<"$out"; then
    echo "A6: FAIL unexpected interrupt"
    fail=1
fi
expect_no_panic A6 "$out" || fail=1
finish A6 $fail "$out"
