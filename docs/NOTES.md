# Learning Notes

Per-milestone log for Part A (bare-metal AArch64 firmware on QEMU `virt`, `secure=on`, Cortex-A53).
For each milestone: what was built, bugs hit, how they were debugged.

Status legend: `[ ]` not started, `[~]` in progress, `[x]` done.

| # | Milestone | Status |
|---|-----------|--------|
| A1 | `start.S`, linker script, Makefile | [x] |
| A2 | PL011 UART driver and minimal printf | [x] |
| A3 | EL3 vector table and exception handler | [ ] |
| A4 | EL3 to EL1 (`eret`), `smc` back to EL3 | [ ] |
| A5 | MMU at Non-secure EL1 | [ ] |
| A6 | GICv3 and generic timer | [ ] |

## Hardware facts to verify

Every address or QEMU behaviour must be checked against the QEMU `virt` documentation
or source (`hw/arm/virt.c`, `hw/arm/boot.c`) before it is used in code.
Fill in the value, the source, and the date once verified. Do not use unverified values.

| Item | Value | Source | Verified |
|------|-------|--------|----------|
| RAM base | 0x4000_0000 (256M with `-m 256M`) | `dumpdtb` memory node, `info mtree` | yes (QEMU 10.0.13) |
| Secure RAM base/size (`secure=on`) | 0x0e00_0000, 16M | `info mtree -f`: `virt.secure-ram` | yes |
| PL011 UART base | 0x0900_0000 (NS UART; 0x0904_0000 is the secure one, disabled in DT) | `dumpdtb` | yes |
| PL011 UARTCLK | 24 MHz (`apb-pclk`, `clock-frequency = 0x16e3600`) → 115200 baud: IBRD=13, FBRD=1 | `dumpdtb` | yes |
| NS UART visible from Secure | yes: `cpu-secure-memory-0` contains NS `pl011` @0x0900_0000 plus secure `pl011` @0x0904_0000 and `virt.secure-ram`; NS `cpu-memory-0` has neither secure region | `info mtree` | yes |
| PL011 register offsets | DR 0x00, FR 0x18, IBRD 0x24, FBRD 0x28, LCR_H 0x2C, CR 0x30, IMSC 0x38 | PL011 TRM (Arm DDI 0183), programmers model | from TRM; DR/FR/CR exercised in QEMU |
| GICv3 distributor base | 0x0800_0000 | `dumpdtb` intc reg | yes |
| GICv3 redistributor base | 0x080a_0000 | `dumpdtb` intc reg | yes |
| Reset state with `secure=on` | EL3h (PSTATE=0x400003cd), PC from loaded image | `-d cpu_reset` | yes |
| Secure flash0 | 0x0000_0000, 64M | `dumpdtb`, `-bios` boot test | yes |
| Generic timer PPI INTID (NS EL1 physical timer) | DT `timer` interrupts list `0x0d, 0x0e, 0x0b, 0x0a`; confirm which is NS EL1 phys (expected 30) in A6 | `dumpdtb` | partly |

## Memory map (final design)

Fill in during A1 once the linker script exists.

| Region | Base | Size | World | Contents |
|--------|------|------|-------|----------|
| Secure flash | 0x0000_0000 | 64M | Secure | `.text`, `.rodata`, and the load image of `.data` |
| Secure RAM | 0x0e00_0000 | 16M | Secure | `.data` (copied at boot), `.bss`, EL3 stack (16K) |
| Non-secure RAM | 0x4000_0000 | 256M | Non-secure | EL1 payload, page tables, EL1 stack |

## Commands cheat sheet

```sh
make debug                              # terminal 1: QEMU paused, gdbstub on :1234
gdb-multiarch build/firmware.elf        # terminal 2
(gdb) target remote :1234
(gdb) hbreak _start                      # hardware bp: secure RAM/flash is not writable by GDB sw breakpoints
(gdb) continue
(gdb) info registers                    # general registers
(gdb) x/10i $pc                         # disassemble around PC
(gdb) stepi                             # single instruction
```

Leave QEMU `-nographic` with `Ctrl-A` then `X`.

---

## A1: Reset entry, linker script, Makefile

**Goal:** CPU starts at `_start`, stack is set, BSS is cleared, control reaches C `main()`.

### What was built
- `linker/link.ld`: `.text`/`.rodata` in secure flash at 0; `.data` has LMA in flash and VMA in secure RAM; `.bss` and a 16K stack in secure RAM; an NS RAM region reserved for A4.
- `src/start.S`: park cores != 0, set `sp`, copy `.data` flash to RAM, clear `.bss`, `bl main`.
- `src/main.c`: probes (`data_probe` in `.data`, `bss_probe` in `.bss`).
- `Makefile` (`all`, `run`, `debug`, `test`, `dis`, `clean`), `scripts/a1.gdb` and `scripts/test_a1.sh`.

### Key concepts
- With `secure=on` the CPU resets into **EL3h** (PSTATE 0x400003cd).
- Flash holds code only; writable state must live in RAM, so `.data` needs separate load and run addresses (`AT>`) and a copy loop at boot. This is what real boot ROMs (e.g. TF-A BL1) do.
- `ldr xN, =symbol` loads a linker symbol from a literal pool.

### Bugs and debugging
| Symptom | Cause | How it was found |
|---------|-------|------------------|
| `-kernel firmware.elf` ran `udf #0` at 0x0e000000 | `-kernel`/`-device loader` write the image into the Non-secure address space; secure RAM stays empty | `-d in_asm,cpu_reset` showed PC=0x0e000000 with zeros; `info mtree` showed secure RAM is a secure-only region |
| GDB `continue` hung with `break` | Software breakpoints write memory, which fails/hangs when the image is not where GDB sees it; also QEMU already stops at reset PC | Use `hbreak`, and check `$pc` at reset instead of continuing to `_start` |
| Fix | Boot with `-bios firmware.bin` (lands in secure flash at 0) | `-d in_asm` showed `start.S` instructions executing at 0x0 |

### Verification
- `make test` (GDB batch): PC == `_start` at reset, CPSR is EL3, `sp == __stack_top`, `.bss` cleared (after dirtying it from GDB), `.data` copied (`data_probe == 0x1234`).
- First manual GDB session: `make debug`, then `target remote :1234`, `stepi` through `start.S`.

---

## A2: PL011 UART driver and minimal printf

**Goal:** polling `uart_putc` / `uart_puts` and a small `printf` with no libc.

### What was built
- `include/mmio.h`: `mmio_read32` / `mmio_write32` through `volatile uint32_t *`; the only way the code touches device registers.
- `src/uart.c`: PL011 driver. `uart_init` follows the TRM sequence (disable, wait `BUSY`, clear `FEN`, IBRD/FBRD, then `LCR_H` = 8N1 + FIFO, mask interrupts, enable `UARTEN|TXE|RXE`). `uart_putc` polls `TXFF` and sends `\r` before `\n`.
- `src/printf.c`: `%c %s %d %u %x %p %%`, `l` modifier for 64-bit, `(null)` for NULL strings, `INT64_MIN` handled by negating in unsigned.
- `src/lib.c`: byte-loop `memset` / `memcpy`.
- `src/main.c`: prints `CurrentEL`, known-value `printf` checks, and A1 cross-check (`data_probe`, `bss_probe` value and address).
- `-mgeneral-regs-only` added to `CFLAGS`; `scripts/test_a2.sh`; `make test` runs A1 and A2.

### Key concepts
- MMIO: devices are addresses; reads/writes have side effects, so every access must be a real, ordered memory access (`volatile`). `volatile` binds the compiler only; with the MMU off all data accesses are Device memory, so the CPU does not cache or reorder them (revisit in A5).
- Secure can access Non-secure memory/devices, not the other way round, so EL3 uses the NS UART (the one `-nographic` connects to the terminal).
- `IBRD`/`FBRD`/`LCR_H` form one 30-bit register latched by the `LCR_H` write, so `LCR_H` must be written last.
- `<stdarg.h>`, `<stdint.h>`, `<stddef.h>` are freestanding headers provided by the compiler, so they work with `-nostdlib`.
- `va_arg(ap, unsigned int)` reads 32 bits; a 64-bit value needs `%lx` (`l` modifier).
- AAPCS64 passes FP args in `v0`-`v7`, so a variadic function saves `q0`-`q7` in its prologue (seen in `make dis`: 8 `str qN`). If FP is trapped (`CPTR_EL3.TFP`) that faults before any output. `-mgeneral-regs-only` removes them (0 `q` registers in the image); the cost is no floating point.
- GCC may emit calls to `memset`/`memcpy` even with `-ffreestanding` (GCC 14: zero-init or struct copy of 512+ bytes, even at `-O0`; 256 bytes is inlined with `stp`). A freestanding build must provide `memcpy`, `memmove`, `memset`, `memcmp`. Checked that `lib.c` at `-O2`/`-O3` does not turn its own loop into a call to itself.

### Bugs and debugging
| Symptom | Cause | How it was found |
|---------|-------|------------------|
| `undefined reference to 'memset'` with `char big[512] = {0}` | GCC lowers large zero-init to a `memset` call; no libc | link error; `objdump -dr` shows `R_AARCH64_CALL26 memset` |
| A1 test `FAIL sp mismatch` (sp=0xe004010) after adding `uart_init()` to `main` | `hbreak main` stops after the prologue; `main` now calls functions so it pushes a 16-byte frame | `objdump` of `main` shows `stp x29, x30, [sp, #-16]!`; fixed with `hbreak *main` |

### Verification
- `make test`: `A1: PASS`, `A2: PASS`; 10 consecutive runs all pass.
- `scripts/test_a2.sh` matches each expected UART line exactly (one `grep -xF` per line); the `bss_probe` address is taken from `nm`, so the A1 layout is checked from inside the firmware.
- Break tests: UART base 0x0904_0000 → no output, every line FAIL (secure UART is not connected to stdio); `%x` in base 10 → only hex lines FAIL. Removing the BSS clear does **not** fail A2 (QEMU RAM starts zeroed); A1's dirty-BSS GDB check is what catches it.

---

## A3: EL3 vector table and exception handler

**Goal:** install `VBAR_EL3`, catch exceptions, print `ESR_EL3`, `ELR_EL3`, `FAR_EL3`.

### What was built
-

### Key concepts
-

### Bugs and debugging
| Symptom | Cause | How it was found |
|---------|-------|------------------|
| | | |

### Verification
- Deliberate faults tried:

---

## A4: EL3 to EL1 with `eret`, `smc` back to EL3

**Goal:** enter Secure EL1, then Non-secure EL1 (`SCR_EL3.NS`), and return a result through `smc` (SMCCC: function ID in `x0`, args `x1`-`x3`).

### What was built
-

### Key concepts
-

### Bugs and debugging
| Symptom | Cause | How it was found |
|---------|-------|------------------|
| | | |

### Verification
-

---

## A5: MMU at Non-secure EL1

**Goal:** hand-written translation tables, Device vs Normal memory attributes.

### What was built
-

### Key concepts
-

### Bugs and debugging
| Symptom | Cause | How it was found |
|---------|-------|------------------|
| | | |

### Verification
-

---

## A6: GICv3 and generic timer

**Goal:** route one PPI (timer) through GICv3 using the `ICC_*` system registers and get periodic interrupts.

### What was built
-

### Key concepts
-

### Bugs and debugging
| Symptom | Cause | How it was found |
|---------|-------|------------------|
| | | |

### Verification
-

---

## Open questions

-
