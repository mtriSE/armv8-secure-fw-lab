# armv8-secure-fw-lab

A hands-on learning project for **secure firmware on ARMv8-A (AArch64)**.

The goal is to understand, from the bottom up, the software that runs at the
highest privilege levels of an Arm system: the code that starts at CPU reset,
handles exceptions and SMC calls, and sets up the secure/non-secure split that
Arm Trusted Firmware-A (TF-A) and OP-TEE are built on.

This project is also preparation for an *Associate Software Engineer (Secure
BSP Firmware)* role, which involves TF-A (BL31), OP-TEE, PSCI, SMC handling,
Trusted Applications and secure boot.

> **Status:** Part A is in progress (see the [Roadmap](#roadmap)). Nothing
> below is final until the matching milestone is marked done. Hardware
> addresses and QEMU behaviors are verified against QEMU documentation/source
> before they are used in code, and are recorded in `docs/NOTES.md`.

## Roadmap

The project has three parts. Only **Part A** is being worked on now.

| Part | Goal | Platform |
|------|------|----------|
| **A** | Bare-metal AArch64 firmware written from scratch | QEMU `virt`, `secure=on`, Cortex-A53 |
| **B** | "Secure Key Vault": TF-A + OP-TEE + Linux, end to end | QEMU, Docker build |
| **C** | U-Boot, Linux driver, device tree, Yocto practice | BeagleBone Black (real hardware) |

### Part A: bare-metal firmware (current)

The firmware starts at CPU reset in **EL3**, with no TF-A and no external
libraries. Each milestone adds one layer.

| # | Milestone | What it teaches |
|---|-----------|-----------------|
| A1 | `start.S`, linker script, Makefile | Reset entry, stack setup, clearing BSS, jumping to C `main()` |
| A2 | PL011 UART driver (polling) | MMIO with `volatile`, `uart_putc` / `uart_puts`, a minimal `printf` |
| A3 | EL3 vector table and exception handler | Exception model; reading `ESR_EL3`, `ELR_EL3`, `FAR_EL3`; deliberate faults |
| A4 | EL3 → EL1 with `eret` (first Secure EL1, then Non-secure EL1 via `SCR_EL3.NS`), then `smc` back to EL3 | Exception levels, secure/non-secure world switch, SMCCC (function ID in `x0`, args in `x1`–`x3`), returning a result |
| A5 | MMU at Non-secure EL1 with hand-written translation tables | Page tables, Device vs. Normal memory attributes |
| A6 | GICv3 and generic timer (one PPI) | Interrupt routing, periodic timer interrupts |

Design decisions for Part A:

- The final payload runs in **Non-secure EL1**, like BL31 handing over to
  BL33/Linux in Part B. The EL3 firmware lives in Secure RAM and the payload in
  Non-secure RAM, so the A1 linker script reserves both regions.
- The interrupt controller is **GICv3** (`-M virt,secure=on,gic-version=3`),
  using the `ICC_*` system registers, not the GICv2 MMIO CPU interface.
- Every milestone has an automated check: a GDB batch script for A1, and
  UART-output pattern checks for A2–A6 (no word-for-word diffs).
  The first GDB session of each milestone is still done by hand.

### Part B: Secure Key Vault (later)

- Custom SiP SMC handler in BL31, plus a Linux kernel module that calls it
- A Trusted Application (OP-TEE) for key storage and signing
- Trusted Board Boot: signed images and a tamper test
- PSCI tracing with GDB
- Automated tests and CI; TF-A and OP-TEE built inside a Docker container

### Part C: real hardware (later)

BeagleBone Black (TI AM335x, Cortex-A8, ARMv7-A 32-bit) for U-Boot, a Linux
driver, device tree and Yocto. It is **not** used for Parts A and B, because it
is not ARMv8 and gives no usable TrustZone for custom code.

## Architecture (Part A, target design)

```
 Host (Debian 13)
 ┌─────────────────────────────────────────────────────────────┐
 │  aarch64-linux-gnu-gcc ──► firmware.elf / firmware.bin      │
 │                                  │                          │
 │                                  ▼                          │
 │  qemu-system-aarch64 -M virt,secure=on -cpu cortex-a53      │
 │  ┌───────────────────────────────────────────────────────┐  │
 │  │  Emulated Arm system                                  │  │
 │  │                                                       │  │
 │  │   EL3  firmware: reset entry, vectors, SMC handler    │  │
 │  │    │ ▲                                                │  │
 │  │ eret │ │ smc                                          │  │
 │  │    ▼ │                                                │  │
 │  │   EL1  test payload: MMU on, timer IRQ                │  │
 │  │                                                       │  │
 │  │   Devices: PL011 UART, GIC, generic timer, RAM        │  │
 │  └───────────────────────────────────────────────────────┘  │
 │        ▲ serial console             ▲ gdbstub (-s -S)       │
 │        └──── terminal               └──── gdb-multiarch     │
 └─────────────────────────────────────────────────────────────┘
```

## Repository layout (planned)

```
armv8-secure-fw-lab/
├── README.md
├── LICENSE             # BSD-3-Clause
├── Makefile
├── docs/
│   └── NOTES.md        # per-milestone log: what was done, bugs, how they were debugged
├── scripts/            # QEMU run / debug helper scripts
├── linker/             # linker script(s)
└── src/                # start.S, C sources, headers with register definitions
```

The exact layout may change as milestones are added.

## Prerequisites

Part A uses native tools on Debian 13 (trixie):

| Tool | Debian package |
|------|----------------|
| `aarch64-linux-gnu-gcc` (built with `-ffreestanding -nostdlib`) | `gcc-aarch64-linux-gnu` |
| `aarch64-linux-gnu-ld`, `objdump`, `objcopy` | `binutils-aarch64-linux-gnu` |
| `qemu-system-aarch64` | `qemu-system-arm` |
| `gdb-multiarch` | `gdb-multiarch` |
| `make`, `git` | `make`, `git` |
| `dtc` | `device-tree-compiler` |

`aarch64-none-elf-gcc` is not packaged in Debian, so the Linux cross toolchain
is used in freestanding mode instead.

```sh
sudo apt install gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
                 qemu-system-arm gdb-multiarch make git device-tree-compiler
```

Part B additionally needs Docker.

## Build and run

> Targets below are the planned interface and will exist once milestone A1 is
> done.

```sh
make            # build the firmware ELF/binary
make run        # run on QEMU, serial output on the terminal
make debug      # run on QEMU paused, waiting for GDB (-s -S)
make test       # run the checks of all finished milestones (regression check)
make clean
```

`make test` will also support repeating the run 10 times to catch flaky results.

Debugging with GDB (from A1 onward):

```sh
# terminal 1
make debug

# terminal 2
gdb-multiarch build/firmware.elf
(gdb) target remote :1234
(gdb) break _start
(gdb) continue
```

Leave QEMU with `Ctrl-A` then `X` when using `-nographic`.

## Code standards

- Embedded-style C: `volatile` for MMIO, no libc, fixed-width integer types
  (`uint32_t`, ...), register definitions in headers
- Code comments in English
- One clear commit per finished milestone

## Working with third-party code (Part B)

- **No upstream source is copied into this repository.** TF-A, OP-TEE and Linux
  are tracked as git submodules, or as a patch series (`git format-patch`)
  with a script that clones upstream and applies the patches. This keeps the
  repository small and keeps the original licenses intact. It also matches the
  real BSP workflow: track upstream updates and merge them into the BSP.
- **License headers:** when changing a TF-A or OP-TEE file, keep the original
  header. Add a copyright line only for a significant change.
- **Keys:** secure-boot keys here are **test keys only**, named clearly (for
  example `test-keys/`). Preferably a script generates them, and **no private
  key is ever committed**.

## License

This project is licensed under the [BSD 3-Clause License](LICENSE).

## Documentation

`docs/NOTES.md` is the learning log. Each milestone records what was built, the
bugs hit, and how they were debugged. Reference material:

- [Arm Architecture Reference Manual for A-profile (Arm ARM)](https://developer.arm.com/documentation/ddi0487/latest)
- [Arm SMC Calling Convention (SMCCC)](https://developer.arm.com/documentation/den0028/latest)
- [Trusted Firmware-A documentation](https://trustedfirmware-a.readthedocs.io/)
- [OP-TEE documentation](https://optee.readthedocs.io/)
- [QEMU `virt` machine documentation](https://www.qemu.org/docs/master/system/arm/virt.html)
