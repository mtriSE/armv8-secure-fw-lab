#!/bin/bash
# A1 regression check: QEMU paused + GDB batch script.
set -u
cd "$(dirname "$0")/.."
qemu-system-aarch64 -M virt,secure=on,gic-version=3 -cpu cortex-a53 -m 256M \
    -nographic -bios build/firmware.bin -s -S >/dev/null 2>&1 &
QPID=$!
sleep 1
out=$(timeout 20 gdb-multiarch -q -batch -x scripts/a1.gdb build/firmware.elf 2>&1)
rc=$?
kill $QPID 2>/dev/null; wait $QPID 2>/dev/null
echo "$out" | grep '^A1:'
echo "$out" | grep -q '^A1: PASS' && [ $rc -eq 0 ]
