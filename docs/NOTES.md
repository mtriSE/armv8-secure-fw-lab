# Learning Notes

Per-milestone log for Part A (bare-metal AArch64 firmware on QEMU `virt`, `secure=on`, Cortex-A53).
For each milestone: what was built, bugs hit, how they were debugged.

Status legend: `[ ]` not started, `[~]` in progress, `[x]` done.

| # | Milestone | Status |
|---|-----------|--------|
| A1 | `start.S`, linker script, Makefile | [x] |
| A2 | PL011 UART driver and minimal printf | [x] |
| A3 | EL3 vector table and exception handler | [x] |
| A4 | EL3 to EL1 (`eret`), `smc` back to EL3 | [x] |
| A5 | MMU at Non-secure EL1 | [x] |
| A6 | GICv3 and generic timer | [x] |

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
| Unmapped hole (secure AS) used as bad address | 0x0b00_0000 (between virtio-mmio end 0x0a00_3fff and secure RAM 0x0e00_0000); load gives sync external abort, DFSC 0x10 | `info mtree -f`, A3 fault test | yes |
| `VBAR_EL3` reset value | 0 in QEMU (architecturally UNKNOWN) | `-d int`: unhandled `brk` goes to PC 0x200 | yes |
| `VBAR_EL3` low bits | QEMU keeps bits [10:0] (a table at 0x1180 still works); hardware treats them as RES0 | A3 `.align 7` experiment | yes |
| `SCTLR_EL3` at reset | 0x00c50838: M=0, A=0, C=0, SA=1, I=0 | printed by A3 | yes |
| Generic timer PPI INTID (NS EL1 physical timer) | 30 (DT `timer` PPIs 13, 14, 11, 10 = secure phys, NS phys, virt, hyp; PPI 14 → INTID 16+14) | `dumpdtb` + A6: `ICC_IAR1_EL1` returns 30 | yes |
| `CNTFRQ_EL0` | 62 500 000 Hz (Cortex-A53 on QEMU; set by QEMU, firmware would program it on hardware) | printed by A6 | yes |
| GICv3 state after EL3 init | `GICD_CTLR=0x37` (EnableGrp0/1NS/1S, ARE_S, ARE_NS), `GICR_WAKER=0` | printed by A6 | yes |

## Memory map (final design)

| Region | Base | Size | World | Contents |
|--------|------|------|-------|----------|
| Secure flash | 0x0000_0000 | 64M | Secure | EL3 `.text`, `.rodata`, load image of `.data`, embedded payload image (`payload_blob`) |
| Secure RAM | 0x0e00_0000 | 16M | Secure | EL3 `.data` (copied at boot; `data_probe` at the base), `.bss`, EL3 stack (16K) |
| Non-secure RAM | 0x4000_0000 | 256M | Non-secure | EL1 payload (copied by EL3): code, data, BSS with the 4KB-aligned L1 table, EL1 stack (16K) |

EL1 virtual map after A5 (Non-secure EL1): 0x0-0x3fff_ffff Device (identity), 0x4000_0000-0x7fff_ffff Normal (identity), 0x8000_0000 unmapped, 0xc000_0000 alias of 0x4000_0000.

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
- `src/vectors.S`: 16-entry table (`.align 11`, entries `.align 7`). Each entry reserves a trap frame, saves x0/x1, puts the entry number in x0 and branches to common code that saves x2-x30, `ELR_EL3`, `SPSR_EL3`, calls `el3_exception_handler(vec, frame)`, restores (ELR/SPSR from the frame) and `eret`s.
- `include/exception.h`: trap frame offsets shared by asm and C (`_Static_assert` checks them), ESR field macros, EC values.
- `src/exception.c`: expected-fault path (record ESR/ELR/FAR, `elr += 4`, return) and panic path (decode, dump x0-x30, halt).
- `src/start.S` installs `VBAR_EL3` after BSS clear, before `bl main`.
- `src/a3_tests.c`: `brk`, `udf`, load from an unmapped address, unaligned load; each compares ELR with the faulting instruction address (`adr` of a local label).
- `include/sysreg.h`: `read_sysreg` / `write_sysreg` macros (`mrs`/`msr` need the register name inside the instruction).

### Key concepts
- Exception entry (hardware): PSTATE → SPSR, preferred return address → ELR, syndrome → ESR (+FAR for aborts), DAIF masked, SPSel=1, PC = VBAR + offset. x0-x30 are **not** saved; software does that.
- Preferred return address: the faulting instruction for aborts/`brk`/`udf`; the next instruction for `svc`/`hvc`/`smc`.
- Vector offset = source group (cur SP0 / cur SPx / lower A64 / lower A32) × 0x200 + type (sync/IRQ/FIQ/SError) × 0x80. EL3h code faults land at 0x200; `smc` from EL1 at 0x400.
- ESR: EC [31:26], IL [25], ISS [24:0]. For data aborts ISS[5:0] = DFSC, ISS[6] = WnR. FAR is UNKNOWN for `brk`/`udf`.
- With the MMU off every data access is Device memory, so an unaligned access faults (DFSC 0x21) even though `SCTLR_EL3.A` = 0.
- Only general registers are saved because everything is built with `-mgeneral-regs-only` (A2).
- Trap frame is 272 bytes (one padding slot) because SP must stay 16-byte aligned (`SCTLR_EL3.SA` = 1).

### Bugs and debugging
| Symptom | Cause | How it was found |
|---------|-------|------------------|
| No output, ~17k identical `Breakpoint` exceptions in 3 s (pre-A3 firmware + `brk`) | `VBAR_EL3` = 0, so the "handler" at 0x200 is the middle of `main`, which runs into the `brk` again | `-d int -D log`: `...to EL3 PC 0x200`; `objdump` shows 0x200 inside `main` |
| Misaligned table (`.align 7`, VBAR=0x1180) still worked in QEMU | QEMU does not ignore VBAR bits [10:0]; real hardware would | A3 printed `VBAR_EL3=0x1180`; added an explicit 2KB check to `test_a3.sh` |
| Panic dump missing when piping `qemu ... -serial stdio` through `timeout` in an ad-hoc shell command | pipe/`timeout` interaction in the experiment, not the firmware | `-serial file:` showed the full dump |

### Verification
- Deliberate faults tried (QEMU 10.0.13):

  | Fault | ESR_EL3 | EC | ISS | FAR |
  |---|---|---|---|---|
  | `brk #0x42` | 0xf2000042 | 0x3c | 0x42 (imm16) | UNKNOWN |
  | `udf #0` | 0x02000000 | 0x00 | 0 | UNKNOWN |
  | load from 0x0b000000 | 0x96000010 | 0x25 | DFSC 0x10 sync external abort | 0xb000000 |
  | unaligned 8-byte load | 0x96000021 | 0x25 | DFSC 0x21 alignment | address+1 |
  | store to 0x0b000000 (panic) | 0x96000050 | 0x25 | WnR=1, DFSC 0x10 | 0xb000000 |
- `make test`: A1, A2, A3 PASS. `test_a3.sh` matches regex lines, checks `el3_vectors` is 2KB aligned and that no panic dump appears.
- Break tests: no `elr += 4` → panic on the second `brk` (expected record already cleared); no `msr vbar_el3` → output stops; `.align 7` → only the alignment check fails.

---

## A4: EL3 to EL1 with `eret`, `smc` back to EL3

**Goal:** enter Secure EL1, then Non-secure EL1 (`SCR_EL3.NS`), and return a result through `smc` (SMCCC: function ID in `x0`, args `x1`-`x3`).

### What was built
- `payload/`: a separate EL1 image (own `start.S`, `link.ld` at 0x4000_0000, EL1 vectors, handler, `main.c`), reusing `uart.c`/`printf.c`/`lib.c`. Built to `build/payload.bin` and embedded into the firmware with `.incbin` (`src/payload_blob.S`).
- `src/el3_payload.c`: EL3 copies the payload to NS RAM (`__ns_start`, reserved in A1), resets EL1 sysregs, sets `SCR_EL3`, and enters it via `el3_eret` (`src/el3_eret.S`: set ELR/SPSR, zero x1-x30, `eret`).
- SMC dispatcher (EL3, vector 0x400, EC 0x17): `SMCCC_VERSION`, SiP `ADD`, `GET_WORLD`, `DONE`, and `SMCCC_UNKNOWN` (-1) for anything else (`include/smccc.h`).
- Flow: Secure EL1 run → `SIP_DONE` → EL3 reloads the payload, clears the trap frame, rewrites ELR/SPSR in it, sets `SCR_EL3.NS` → the same `eret` lands in Non-secure EL1 → `SIP_DONE` → EL3 exits QEMU via semihosting with the failure count as exit status.
- `include/vector_table.h`: the A3 vector table turned into a macro used for both EL3 and EL1.
- Tests: `scripts/lib.sh` (shared QEMU run + line matching), `test_a4.sh`; `make test` runs every `scripts/test_aN.sh`. Semihosting exit made the whole suite take ~1.6 s instead of 5 s per test.

### Key concepts
- There is no "go to EL1" instruction: EL3 fakes an exception return. `ELR_EL3` = entry, `SPSR_EL3` = 0x3c5 (EL1h, DAIF masked), `SCR_EL3` = RW (AArch64 below) + NS (world), then `eret`.
- `SCR_EL3.NS` decides the world of everything below EL3. It does not change EL3's own accesses (EL3 is always Secure in AArch64).
- SMC: preferred return address is already the next instruction, so the dispatcher must **not** add 4 (break test: adding 4 corrupts results). Results go back by writing `tf->x[0]`; the common exit code restores registers from the frame.
- SMCCC function ID: bit 31 fast call, bit 30 SMC64, bits [29:24] owner (2 = SiP), [15:0] number → `0xC2000001`... Only w0 is significant.
- EL1 system registers (SCTLR_EL1, VBAR_EL1, TTBR0_EL1, SP_EL1...) are **not banked** between worlds. EL3 must reset (or, in real firmware, save/restore) them on a world switch, and clear general registers so Secure values do not leak to Non-secure (TF-A: context management).
- TrustZone in action: Secure EL1 reads Secure RAM (val 0x1234 = EL3 `data_probe`); the same `ldr` from Non-secure EL1 gets a synchronous external abort (EC 0x25, DFSC 0x10, FAR 0xe000000), taken at EL1 because `SCR_EL3.EA` = 0. The NS address space simply has no Secure RAM in it (`info mtree`).
- `SCR_EL3.SIF` is left 0 because the Secure EL1 run executes from NS RAM; real firmware sets SIF=1.
- After copying code, `dsb; ic iallu; dsb; isb` so no stale instructions are fetched (no effect in QEMU, required on hardware with caches on).
- No EL2 on QEMU `virt` without `virtualization=on`, so EL3 goes straight to EL1.

### Bugs and debugging
| Symptom | Cause | How it was found |
|---------|-------|------------------|
| `NULL undeclared` in `payload/main.c` | missing `<stddef.h>` | build error |
| (design) `hlt #0xf000` would be UNDEF without `-semihosting` and trigger a panic | every QEMU command line must enable semihosting | moved the QEMU command into `scripts/lib.sh` |

### Verification
- `make test`: A1–A4 PASS. `test_a4.sh` checks both runs line by line, `el1_vectors` 2KB alignment, and no panic.
- Break tests: `SCR_NS` never set → all `[NS]` lines missing (the payload keeps running Secure); `tf->elr += 4` after SMC → SMC results wrong from the first call.

---

## A5: MMU at Non-secure EL1

**Goal:** hand-written translation tables, Device vs Normal memory attributes.

### What was built
- `payload/mmu.c`: one level-1 table (4 entries, 1GB blocks), 4KB granule, T0SZ=32 (32-bit VA, so the walk starts at level 1), TTBR1 walks disabled (EPD1).

  | VA | PA | Type |
  |---|---|---|
  | 0x0000_0000 | 0x0000_0000 | Device-nGnRnE, PXN+UXN (UART, GIC) |
  | 0x4000_0000 | 0x4000_0000 | Normal WB, inner shareable (payload) |
  | 0x8000_0000 | — | invalid (fault test) |
  | 0xC000_0000 | 0x4000_0000 | alias of the RAM block |
- Enable sequence: write table, `dsb sy`; MAIR/TCR/TTBR0, `isb`; `tlbi vmalle1; dsb nsh; isb`; SCTLR_EL1 |= M|C|I; `isb`.
- `payload/a5.c` checks, run only in the Non-secure world.

### Key concepts
- MAIR_EL1 holds up to 8 memory types (attr0 = 0x00 Device-nGnRnE, attr1 = 0xff Normal write-back RW-allocate); a descriptor picks one with AttrIndx[4:2].
- Block descriptor: bits[1:0]=01, AttrIndx, AP[7:6] (00 = EL1 RW), SH[9:8], **AF[10]** (must be 1, or the first access faults), PXN[53]/UXN[54], output address.
- Device vs Normal: the same unaligned 8-byte load faults with the MMU off (everything is Device, DFSC 0x21) and works once RAM is mapped Normal (SCTLR_EL1.A = 0). Closes the loop from A2/A3.
- Device regions are marked execute-never: speculative instruction fetches must never touch devices.
- Translation fault on a missing descriptor: DFSC 0x05 = level 1 (the level where the walk stopped).
- `isb` after SCTLR write: the next instruction is fetched through the MMU; identity mapping of the running code is what makes that safe.
- TLB invalidate before enabling: EL1 translation state is shared between worlds (A4), so stale entries from the Secure run must not be used.
- Tables were written with caches off, so they are in memory when the walker (configured cacheable, IRGN/ORGN=WBWA) reads them.

### Bugs and debugging
| Symptom | Cause | How it was found |
|---------|-------|------------------|
| (break test) no output after "before MMU", QEMU spins until timeout | AF=0: first instruction fetch after enabling the MMU → instruction abort EC 0x21, IFSC 0x09 (access flag, level 1); the EL1 vectors are in the same block, so the handler fetch faults too (~422k exceptions in 3 s) | `-d int`: `ESR 0x21/0x86000009` repeating |
| `test_a5.sh` regex never matched the alias line | `0xc$H` with `H='0x[0-9a-f]+'` doubles the `0x` | test output vs pattern |

### Verification
- `make test`: A1–A5 PASS. Output: `SCTLR_EL1=0x30d01805` (M, C, I set), `TCR_EL1=0x803520`, `MAIR_EL1=0xff00`; alias `0xc0003010 -> 0x40003010` reads the value written through the identity mapping; `0x80000000` faults with DFSC 0x5, FAR 0x80000000.
- Break tests: AF cleared → silent exception storm (above), test FAILs; RAM attr changed to Device in MAIR → unaligned load still faults after MMU on, test FAILs.

---

## A6: GICv3 and generic timer

**Goal:** route one PPI (timer) through GICv3 using the `ICC_*` system registers and get periodic interrupts.

### What was built
- `src/gic_el3.c` (Secure, before any lower EL runs): `ICC_SRE_EL3` = SRE|DFB|DIB|Enable; `GICD_CTLR` = ARE_S|ARE_NS, wait RWP, then also enable Grp0/Grp1NS/Grp1S; wake the redistributor (`GICR_WAKER.ProcessorSleep` = 0, wait `ChildrenAsleep` = 0); INTID 30 → Group 1 Non-secure (`IGROUPR0` bit 30 = 1, `IGRPMODR0` bit 30 = 0).
- `payload/gic.c` (NS EL1 "driver"): `ICC_SRE_EL1.SRE`, `ICC_PMR_EL1` = 0xff, `ICC_IGRPEN1_EL1` = 1, priority byte + `GICR_ISENABLER0` for INTID 30, ack via `ICC_IAR1_EL1`, `ICC_EOIR1_EL1` for EOI.
- `payload/a6.c`: NS EL1 physical timer, 10 ms period (`CNTP_TVAL_EL0` = CNTFRQ/100, `CNTP_CTL_EL0.ENABLE`), `msr daifclr, #2`, `wfi` until 5 ticks; the IRQ vector (EL1 0x280) calls `irq_handler`.
- `include/gic.h`: register offsets shared by both sides.

### Key concepts
- GICv3 has three parts: Distributor (global, SPIs), one Redistributor per CPU (SGIs/PPIs, power state) at RD_base with the SGI_base frame 64KB above, and the CPU interface as system registers (`ICC_*`).
- With security enabled (`GICD_CTLR.DS` = 0) the **group** of an interrupt is Secure-only state. EL3 decides which interrupts the Normal world owns; the NS driver can only enable/prioritise those. Same split as TF-A (`plat_gic` setup in BL31) vs Linux.
- Group 1 NS interrupts are signalled as IRQ to Non-secure EL1 (with `SCR_EL3.IRQ` = 0). Group 0 would be FIQ.
- `SCR_EL3.IRQ` = 1 does more than routing: NS EL1 accesses to the Group 1 CPU-interface registers then trap to EL3 (EC 0x18).
- Timer output is level-sensitive: reprogram (`TVAL`) or disable it **before** EOI, or the interrupt is immediately pending again.
- `ICC_IAR1_EL1` read = acknowledge (INTID becomes active); 1023 = spurious, no EOI. `ICC_EOIR1_EL1` write = priority drop + deactivate (EOImode 0).
- Reset state has DAIF = 1111 (A1): IRQs must be unmasked explicitly (`daifclr, #2`). `wfi` sleeps until an interrupt is pending.
- `isb` before reading `CNTPCT_EL0`, otherwise the counter may be read early.
- GIC MMIO is reachable after A5 because the first 1GB is mapped Device-nGnRnE.

### Bugs and debugging
| Symptom | Cause | How it was found |
|---------|-------|------------------|
| (break test) no ticks, run ends by timeout | EL3 left INTID 30 in Group 0 → signalled as FIQ (Secure-owned), never as IRQ to NS EL1; `IGROUPR0` printed 0 | A6 EL3 line + test |
| (break test) 71 565 "ticks" in 3 s, never finishes | timer not reprogrammed/stopped before EOI → level interrupt re-asserts at once | UART output |
| (break test) EL3 panic `low64_sync`, `ESR_EL3=0x623e3018`, EC 0x18 | `SCR_EL3.IRQ` = 1: the NS write to `ICC_IGRPEN1_EL1` traps to EL3. ISS decodes to Op0=3 Op1=0 CRn=12 CRm=12 Op2=7, write = `S3_0_C12_C12_7` | EL3 panic dump; added EC 0x18 to the decoder |

### Verification
- Output: `GICD_CTLR=0x37 GICR_WAKER=0x0 IGROUPR0=0x40000000 IGRPMODR0=0x0`, `CNTFRQ_EL0=62500000`, ticks 1–5 with `intid=30`, `5 timer IRQs in 52 ms` (≥ 5 × 10 ms required).
- `make test`: A1–A6 PASS; 10 consecutive runs all pass; the full suite takes ~2 s.
- Break tests: the three above, each FAILs.

---

## B1: Baseline secure stack (TF-A + OP-TEE + U-Boot + Linux), built in Docker

**Goal:** the unmodified upstream stack boots on QEMU `virt` and passes an `xtest` smoke set, built by our own `bsp/Makefile` inside a pinned container. Nothing upstream is changed yet.

### What was built
- `third_party/`: shallow submodules at pinned tags: TF-A `lts-v2.14.14`, OP-TEE os/client/test `4.11.0`, U-Boot `v2026.10`, Linux `v6.18.63`, Buildroot `2026.02.3`. TF-A has nested submodules (`contrib/dtc`, `mbed-tls`, ...): clone with `--recurse-submodules`.
- `bsp/docker/Dockerfile`: Debian 13 by digest + Arm GNU Toolchain 14.3.rel1 (`aarch64-none-linux-gnu`, sha256-checked). `bsp/dock` runs commands in it as the host user, image tag = hash of the Dockerfile.
- `bsp/Makefile`: one target per component, all out of tree (`O=` / `BUILD_BASE=`) into `bsp/out/`, so the submodules stay clean. Flags taken from the OP-TEE reference build (`OP-TEE/build` `qemu_v8.mk` 4.11.0) and commented.
- `bsp/configs/`: U-Boot and Linux Kconfig fragments (merged with `merge_config.sh`), Buildroot defconfig (external toolchain, BusyBox, gzip initramfs).
- `bsp/br-ext/`: Buildroot external tree; `optee-client-lab` (libteec, tee-supplicant run as user `teesuppl`, not root) and `optee-test-lab` (xtest + 21 test TAs built against our TA dev kit).
- `bsp/scripts/test_b1.sh` + `b1.exp`: boot, log in, `xtest -t regression 1001 1004 4001 6001`, `poweroff`; then pattern checks on both UART logs.
- `.github/workflows/ci.yml`: Part A in a `debian:trixie-slim` container; Part B builds and boots in the `bsp/dock` image.

### Key concepts
- **Boot flow** (from the log): BL1 (EL3, ROM at flash 0) loads BL2 → `BL1: Booting BL2` with `SPSR = 0x3c5` (S-EL1h, DAIF masked). BL2 runs at **S-EL1**, loads BL31 (image id 3, 0x0e090000), BL32 = OP-TEE (id 4 header + id 21 pager, 0x0e100000) and BL33 = U-Boot (id 5, 0x60000000). BL2 cannot enter EL3 itself, so it SMCs back to BL1, which ERETs into BL31: `BL1: Booting BL31`, `SPSR = 0x3cd` (EL3h).
- BL31 then runs OP-TEE init first (S-EL1, via SPD `opteed`), OP-TEE returns with an SMC (`Primary CPU switching to normal world boot`), and BL31 ERETs to U-Boot at NS-EL1. Same sequence as A4 (Secure EL1 pass, then Non-secure), now done by TF-A.
- `WARNING: Firmware Image Package header check failed`: expected. There is no FIP in flash (only `bl1.bin` via `-bios`), so TF-A falls back to semihosting and reads `bl2.bin`, `bl31.bin`, `bl32*.bin`, `bl33.bin` from QEMU's working directory. U-Boot loads `Image` and `rootfs.cpio.gz` the same way (`load hostfs`). QEMU-only: a board reads eMMC/SPI flash.
- Memory: OP-TEE owns secure DRAM 0x0e100000–0x0effffff (`CFG_TZDRAM_*`); its static shared memory with Linux is NS RAM **0x42000000–0x421fffff** (`CFG_SHMEM_*`). Both appear as `nomap` reserved-memory in the Linux log; OP-TEE adds them to the DTB before it is handed to U-Boot. U-Boot's default `kernel_addr_r` (0x40400000) would put the 41 MB Image over the SHM, so we load it at 0x42200000.
- Secure world logs go to UART1 (0x09040000, `-serial file:secure.log`): with `secure=on` that UART is only mapped in the Secure address space, so Linux cannot see or use it.
- `tee-supplicant`: OP-TEE has no filesystem; to load a TA (`/lib/optee_armtz/<uuid>.ta`) or store secure-storage files (`/var/lib/tee`) it makes an RPC to Linux, served by this daemon through `/dev/teepriv0`. Clients use `/dev/tee0`.
- `poweroff` → Linux PSCI `SYSTEM_OFF` (SMC) → BL31 → QEMU exits: the first PSCI call we observe (B2 traces `CPU_ON`).
- Buildroot external toolchain: Buildroot checks the declared toolchain (GCC 14, headers 4.20, glibc, C++, Fortran, OpenMP) against the real one and refuses any mismatch.

### Bugs and debugging
| Symptom | Cause | How it was found |
|---------|-------|------------------|
| TF-A: `contrib/dtc/libfdt/Makefile.libfdt: No such file` | TF-A ≥ 2.12 has its own submodules (dtc, libtl, mbed-tls, ...) | `.gitmodules` in TF-A; `git submodule update --init --depth 1` |
| Buildroot: `Fortran support is not selected but is available in external toolchain` | external toolchain declaration must match exactly | error text; added `FORTRAN`/`OPENMP` (as the reference config does) |
| Buildroot: `cannot stat .../out/ta/*/*.ta` | Buildroot builds CMake packages in source; CMake overwrote `optee_test/ta/Makefile`, so `make -C ta` did nothing | empty make output; optee_test keeps the original as `ta/Makefile.gmake` (`-f Makefile.gmake`) |
| QEMU: `failed to find romfile "efi-virtio.rom"` | default virtio NIC needs the iPXE ROM (package not in the image) | `console.log`; `-nic none`, no network needed |
| Test FAIL on TF-A lines | guessed patterns (`BL2: Booting BL2`) before the first real boot | real log: BL1 boots both BL2 and BL31 (see above) |

### Verification
- `./bsp/dock make -C bsp test`: B1 PASS twice in a row, ~40 s per boot. xtest: `631 subtests of which 0 failed`, `4 test cases of which 0 failed`.
- Part A `make test`: A1–A6 still PASS. All submodules have 0 changes after the build.
- Sizes: sources 2.0 GB Linux + 0.6 GB others; build output ~3.3 GB (`bsp/out/linux` 1.6 GB, `bsp/out/buildroot` 1.2 GB); image 2.2 GB. Image 41 MB, rootfs.cpio.gz 9.8 MB.
- CI workflow written but not run yet (needs a push).

---

## Open questions

- TF-A warns `cortex_a53: CPU workaround for erratum 835769/855873/1530924 was missing`: the `ERRATA_A53_*` build flags are off for PLAT=qemu. Harmless on QEMU; on silicon they are required. Look at how a real platform enables them (B2).
