#!/bin/bash
# A2 regression check: run the firmware, capture UART output, match each
# expected line (pattern match per line, not a word-for-word diff).
set -u
cd "$(dirname "$0")/.."

# The firmware never exits (wfe loop), so timeout is what stops QEMU.
out=$(timeout 5 qemu-system-aarch64 -M virt,secure=on,gic-version=3 -cpu cortex-a53 \
    -m 256M -display none -serial stdio -monitor none -bios build/firmware.bin \
    </dev/null 2>/dev/null | tr -d '\r')

# Address of bss_probe as placed by the linker, to cross-check %p.
bss_addr=$(aarch64-linux-gnu-nm build/firmware.elf | awk '$3 == "bss_probe" { print $1 }' \
    | sed 's/^0*//')

expected=(
    "A2: hello from EL3"
    "A2: CurrentEL=3"
    "A2: hex=0xdeadbeef"
    "A2: hex64=0x123456789abcdef0"
    "A2: dec=-42 zero=0 umax=4294967295"
    "A2: dec64min=-9223372036854775808"
    "A2: str=ok null=(null) chr=Z pct=%"
    "A2: data_probe=0x1234 bss_probe=0x0 bss_probe@0x$bss_addr"
    "A2: done"
)

fail=0
for line in "${expected[@]}"; do
    if ! grep -qxF -- "$line" <<<"$out"; then
        echo "A2: FAIL missing: $line"
        fail=1
    fi
done

if [ $fail -ne 0 ]; then
    echo "A2: --- UART output was ---"
    echo "$out"
    exit 1
fi
echo "A2: PASS"
