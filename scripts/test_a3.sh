#!/bin/bash
# A3 regression check: deliberate EL3 faults must be caught with the right
# EC/ISS, and the firmware must continue to "A3: done" (no panic dump).
set -u
cd "$(dirname "$0")/.."

out=$(timeout 5 qemu-system-aarch64 -M virt,secure=on,gic-version=3 -cpu cortex-a53 \
    -m 256M -display none -serial stdio -monitor none -bios build/firmware.bin \
    </dev/null 2>/dev/null | tr -d '\r')

vbar=$(aarch64-linux-gnu-nm build/firmware.elf | awk '$3 == "el3_vectors" { print $1 }' \
    | sed 's/^0*//')
H='0x[0-9a-f]+'

# Extended regexes, each must match one whole output line.
expected=(
    "A3: VBAR_EL3=0x$vbar SCTLR_EL3=$H"
    "A3: brk: taken=1 EC=0x3c ISS=0x42 ELR=$H FAR=$H OK"
    "A3: udf: taken=1 EC=0x0 ISS=0x0 ELR=$H FAR=$H OK"
    "A3: bad load: taken=1 EC=0x25 ISS=0x10 ELR=$H FAR=0xb000000 OK"
    "A3: unaligned: taken=1 EC=0x25 ISS=0x21 ELR=$H FAR=$H OK"
    "A3: done"
)

fail=0
# QEMU keeps the low bits of VBAR_EL3, so a misaligned table still "works"
# there; real hardware ignores bits [10:0]. Check the alignment explicitly.
if (( 0x$vbar % 0x800 != 0 )); then
    echo "A3: FAIL el3_vectors=0x$vbar is not 2KB aligned"
    fail=1
fi
for re in "${expected[@]}"; do
    if ! grep -qxE -- "$re" <<<"$out"; then
        echo "A3: FAIL missing: $re"
        fail=1
    fi
done
if grep -qF '*** EL3 exception' <<<"$out"; then
    echo "A3: FAIL unexpected exception (panic dump in output)"
    fail=1
fi

if [ $fail -ne 0 ]; then
    echo "A3: --- UART output was ---"
    echo "$out"
    exit 1
fi
echo "A3: PASS"
