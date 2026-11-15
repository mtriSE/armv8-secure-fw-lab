# Learning Notes

Per-milestone log for Part A (bare-metal AArch64 firmware on QEMU `virt`, `secure=on`, Cortex-A53).
For each milestone: what was built, bugs hit, how they were debugged.

Status legend: `[ ]` not started, `[~]` in progress, `[x]` done.

| # | Milestone | Status |
|---|-----------|--------|
| A1 | `start.S`, linker script, Makefile | [x] |
| A2 | PL011 UART driver and minimal printf | [ ] |
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
