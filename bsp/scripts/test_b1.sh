#!/bin/bash
# B1 regression check: the unmodified stack boots through every stage, OP-TEE
# is found by Linux, the xtest smoke set passes, and poweroff works (PSCI).
# Run through bsp/Makefile ("make -C bsp test"), which sets BIN and QEMU_FLAGS.
set -u
cd "$(dirname "$0")"

: "${BIN:?run via make -C bsp test}" "${QEMU_FLAGS:?run via make -C bsp test}"
# Smoke set: 1001 core self-tests, 1004 TA load and invoke, 4001 hash
# (crypto), 6001 secure storage (REE FS through tee-supplicant).
export XTEST_ARGS=${XTEST_ARGS:-"-t regression 1001 1004 4001 6001"}

rm -f "$BIN/console.log" "$BIN/secure.log"
fail=0
./b1.exp | grep '^B1:' ; [ "${PIPESTATUS[0]}" -eq 0 ] || fail=1
con=$(tr -d '\r' <"$BIN/console.log" 2>/dev/null)
sec=$(tr -d '\r' <"$BIN/secure.log" 2>/dev/null)

# check NAME TEXT REGEX: REGEX (extended) must match a line of TEXT.
check() {
    if ! grep -qE -- "$3" <<<"$2"; then
        echo "B1: FAIL missing ($1): $3"
        fail=1
    fi
}

# Boot stages, in order of appearance. BL2 runs at S-EL1, so it cannot enter
# EL3 itself: it hands BL31's entry point back to BL1 with an SMC, and BL1
# (still EL3) jumps there. Hence "BL1: Booting BL31", not "BL2: ...".
check console "$con" '^NOTICE:  Booting Trusted Firmware'
check console "$con" '^NOTICE:  BL1: Booting BL2'
check console "$con" '^NOTICE:  BL2: v2\.14\.14'
check console "$con" '^NOTICE:  BL1: Booting BL31'
check console "$con" '^NOTICE:  BL31: v2\.14\.14'
check secure  "$sec" 'I/TC: OP-TEE version: 4\.11\.0'
check secure  "$sec" 'I/TC: Primary CPU switching to normal world boot'
check console "$con" '^U-Boot 2026\.10'
check console "$con" 'Booting Linux on physical CPU'
check console "$con" 'psci: PSCIv1\.1 detected in firmware'
check console "$con" 'optee: revision 4\.11'
check console "$con" 'optee: initialized driver'
check console "$con" 'Starting tee-supplicant: OK'
check console "$con" '^[0-9]+ subtests of which 0 failed'
check console "$con" '^[0-9]+ test cases of which 0 failed'
check console "$con" 'reboot: Power down'

if grep -qE 'PANIC|Unhandled Exception|Kernel panic' <<<"$con$sec"; then
    echo "B1: FAIL panic in the logs"
    fail=1
fi

if [ $fail -ne 0 ]; then
    echo "B1: logs: $BIN/console.log $BIN/secure.log"
    exit 1
fi
echo "B1: PASS"
