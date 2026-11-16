#!/bin/bash
# A1 regression check: QEMU paused + GDB batch script.
set -u
cd "$(dirname "$0")/.."
qemu-system-aarch64 -M virt,secure=on,gic-version=3 -cpu cortex-a53 -m 256M \
    -nographic -nic none -bios build/firmware.bin -s -S >/dev/null 2>build/a1-qemu.err &
QPID=$!
sleep 1
out=$(timeout 20 gdb-multiarch -q -batch -x scripts/a1.gdb build/firmware.elf 2>&1)
rc=$?
kill $QPID 2>/dev/null; wait $QPID 2>/dev/null
echo "$out" | grep '^A1:'
if echo "$out" | grep -q '^A1: PASS' && [ $rc -eq 0 ]; then
    exit 0
fi
# No PASS: say so, with QEMU's and GDB's own errors (e.g. QEMU did not start).
echo "A1: FAIL (gdb rc=$rc)"
sed 's/^/A1:   qemu: /' build/a1-qemu.err
echo "$out" | grep -v '^A1:' | sed 's/^/A1:   gdb: /' | tail -5
exit 1
