# Shared helpers for the UART-output tests (sourced by scripts/test_aN.sh).
# shellcheck shell=bash
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/.."

# Run the firmware once and print its UART output (CR stripped). The firmware
# ends the run itself through semihosting; timeout only guards against hangs.
run_firmware() {
    timeout 10 qemu-system-aarch64 -M virt,secure=on,gic-version=3 -cpu cortex-a53 \
        -m 256M -display none -serial stdio -monitor none -semihosting \
        -bios build/firmware.bin </dev/null 2>/dev/null | tr -d '\r'
}

# Address of a firmware/payload symbol without leading zeros: sym_addr ELF NAME
sym_addr() {
    aarch64-linux-gnu-nm "$1" | awk -v n="$2" '$3 == n { print $1 }' | sed 's/^0*//'
}

# expect_lines TAG MODE OUTPUT LINE...
# MODE F: fixed strings, E: extended regexes. Each LINE must match one whole
# output line. Prints a FAIL line per missing LINE; returns 1 if any is missing.
expect_lines() {
    local tag=$1 mode=$2 out=$3 fail=0 line
    shift 3
    for line in "$@"; do
        if ! grep -qx"$mode" -- "$line" <<<"$out"; then
            echo "$tag: FAIL missing: $line"
            fail=1
        fi
    done
    return $fail
}

# expect_no_panic TAG OUTPUT: no EL3/EL1 panic dump in the output.
expect_no_panic() {
    if grep -qE '^\*\*\* EL[13] exception' <<<"$2"; then
        echo "$1: FAIL unexpected exception (panic dump in output)"
        return 1
    fi
}

# finish TAG FAIL OUTPUT: print PASS, or the UART output for debugging.
finish() {
    if [ "$2" -ne 0 ]; then
        echo "$1: --- UART output was ---"
        echo "$3"
        exit 1
    fi
    echo "$1: PASS"
}
