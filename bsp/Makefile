# Part B: build and run the secure stack on QEMU virt.
#
#   TF-A BL1 -> BL2 -> BL31 (EL3) -> OP-TEE (BL32, S-EL1) -> U-Boot (BL33) -> Linux
#
# Run inside the build container:   ./bsp/dock make -C bsp [target]
#
# Every component is built out of tree into out/, so third_party/ (the pinned
# upstream submodules) stays clean. Flags follow the OP-TEE reference build
# (OP-TEE/build qemu_v8.mk, tag 4.11.0); each one is explained where it is set.

ifndef BSP_CONTAINER
$(error Run this inside the build container: ./bsp/dock make -C bsp $(MAKECMDGOALS))
endif

ROOT   := $(abspath $(CURDIR)/..)
TP     := $(ROOT)/third_party
OUT    := $(CURDIR)/out
BIN    := $(OUT)/bin
JOBS   ?= $(shell nproc)
ifeq ($(JOBS),)
JOBS   := $(shell nproc)
endif
DEBUG  ?= 1
SMP    ?= 2

# One toolchain for everything (installed in the container, see docker/Dockerfile).
CROSS_COMPILE := aarch64-none-linux-gnu-

# Version strings must not depend on how the sources were checked out.
# Upstream builds call "git describe", which needs tags; CI fetches submodules
# by commit (depth 1, no tags), so the same source gave "4.11.0" locally but
# "f54190f" in CI. Instead use the version each project declares in its own
# makefiles plus the pinned submodule commit, which is the same everywhere.
src_rev = $(shell git -C $(TP)/$(1) rev-parse --short=12 HEAD)
mk_var  = $(shell sed -n 's/^$(2)[[:space:]]*[:?]*=[[:space:]]*//p' $(TP)/$(1))

.PHONY: all optee-os u-boot tf-a buildroot linux bin run debug test clean
all: bin

################################################################################
# OP-TEE OS (BL32): the Secure EL1 trusted OS
################################################################################
# PLATFORM=vexpress-qemu_armv8a: QEMU virt is described in OP-TEE's plat-vexpress.
# CFG_ARM64_core=y:   the core runs in AArch64 S-EL1.
# CFG_USER_TA_TARGETS=ta_arm64: only build the 64-bit TA dev kit (no AArch32 compiler).
# CFG_ARM_GICV3=y:    QEMU runs with gic-version=3 (same as Part A).
# DEBUG/LOG_LEVEL:    debug build with info logs on the secure UART.
OPTEE_OUT := $(OUT)/optee_os
OPTEE_FLAGS := -C $(TP)/optee_os O=$(OPTEE_OUT) \
	PLATFORM=vexpress-qemu_armv8a \
	CFG_ARM64_core=y CFG_USER_TA_TARGETS=ta_arm64 CFG_ARM_GICV3=y \
	CROSS_COMPILE=$(CROSS_COMPILE) CROSS_COMPILE_core=$(CROSS_COMPILE) \
	CROSS_COMPILE_ta_arm64=$(CROSS_COMPILE) \
	DEBUG=$(DEBUG) CFG_TEE_CORE_LOG_LEVEL=3 \
	TEE_IMPL_VERSION=$(call mk_var,optee_os/mk/config.mk,CFG_OPTEE_REVISION_MAJOR).$(call mk_var,optee_os/mk/config.mk,CFG_OPTEE_REVISION_MINOR)-g$(call src_rev,optee_os)
# TA dev kit: headers, libs and signing script that TAs (optee_test, B4) build against.
TA_DEV_KIT := $(OPTEE_OUT)/export-ta_arm64

optee-os:
	$(MAKE) $(OPTEE_FLAGS) -j$(JOBS)

################################################################################
# U-Boot (BL33): the Non-secure bootloader that loads Linux
################################################################################
UBOOT_OUT   := $(OUT)/u-boot
# LOCALVERSION= (set but empty) + LOCALVERSION_AUTO off (u-boot.config):
# the version is exactly the one in U-Boot's Makefile, with no git suffix.
UBOOT_FLAGS := -C $(TP)/u-boot O=$(UBOOT_OUT) CROSS_COMPILE=$(CROSS_COMPILE) LOCALVERSION=

# qemu_arm64_defconfig + our fragment (configs/u-boot.config), merged with the
# kernel-style merge_config.sh, which warns when a requested value is dropped.
$(UBOOT_OUT)/.config: configs/u-boot.config
	mkdir -p $(UBOOT_OUT)
	cd $(TP)/u-boot && ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) \
		scripts/kconfig/merge_config.sh -O $(UBOOT_OUT) \
		configs/qemu_arm64_defconfig $(CURDIR)/configs/u-boot.config

u-boot: $(UBOOT_OUT)/.config
	$(MAKE) $(UBOOT_FLAGS) -j$(JOBS)

################################################################################
# TF-A: BL1 (ROM), BL2 (loader), BL31 (EL3 runtime firmware)
################################################################################
# PLAT=qemu:          QEMU virt port (plat/qemu/qemu).
# SPD=opteed:         the Secure Payload Dispatcher in BL31 that switches between
#                     Linux and OP-TEE on SMC (the industrial version of A4's dispatcher).
# BL32*:              OP-TEE "v2" images: header + pager + pageable parts.
# BL32_RAM_LOCATION=tdram: OP-TEE runs from secure DRAM 0x0e100000 (SEC_DRAM_BASE).
# BL33:               U-Boot, loaded at NS_IMAGE_OFFSET = 0x60000000.
# QEMU_USE_GIC_DRIVER=QEMU_GICV3: matches gic-version=3.
# BUILD_STRING:      shown after the version ("v2.14.14(debug):<commit>").
# "all fip": also build the FIP container (used for Trusted Board Boot in B5).
TFA_OUT   := $(OUT)/tf-a/qemu/$(if $(filter 1,$(DEBUG)),debug,release)
TFA_FLAGS := -C $(TP)/trusted-firmware-a BUILD_BASE=$(OUT)/tf-a \
	CROSS_COMPILE=$(CROSS_COMPILE) PLAT=qemu DEBUG=$(DEBUG) LOG_LEVEL=40 \
	SPD=opteed \
	BL32=$(OPTEE_OUT)/core/tee-header_v2.bin \
	BL32_EXTRA1=$(OPTEE_OUT)/core/tee-pager_v2.bin \
	BL32_EXTRA2=$(OPTEE_OUT)/core/tee-pageable_v2.bin \
	BL32_RAM_LOCATION=tdram \
	BL33=$(UBOOT_OUT)/u-boot.bin \
	QEMU_USE_GIC_DRIVER=QEMU_GICV3 \
	BUILD_STRING=$(call src_rev,trusted-firmware-a)

tf-a: optee-os u-boot
	$(MAKE) $(TFA_FLAGS) -j$(JOBS) all fip

################################################################################
# Buildroot: root filesystem (initramfs) with tee-supplicant, libteec, xtest
################################################################################
# br-ext/ is our BR2_EXTERNAL tree: it builds optee_client and optee_test from
# the third_party/ submodules (same version as optee_os), and the TAs against
# the TA dev kit exported by our optee_os build.
# BR2_DL_DIR: keep downloaded tarballs in out/ (cached in CI).
BR_OUT  := $(OUT)/buildroot
BR_ENV  := BR2_DL_DIR=$(OUT)/dl
BR_MAKE := $(BR_ENV) $(MAKE) -C $(TP)/buildroot O=$(BR_OUT) \
	BR2_EXTERNAL=$(CURDIR)/br-ext LAB_TA_DEV_KIT_DIR=$(TA_DEV_KIT)

$(BR_OUT)/.config: configs/buildroot_defconfig
	$(BR_MAKE) BR2_DEFCONFIG=$(CURDIR)/configs/buildroot_defconfig defconfig

buildroot: $(BR_OUT)/.config optee-os
	# optee_* are local (source) packages: forget their stamps so source
	# changes in third_party/ are always picked up.
	rm -rf $(BR_OUT)/build/optee-client-lab-* $(BR_OUT)/build/optee-test-lab-*
	$(BR_MAKE)

################################################################################
# Linux (Non-secure EL1)
################################################################################
# arm64 defconfig + configs/linux.config (OP-TEE driver, GDB scripts).
# Only the Image target is built; no modules are needed in B1.
LINUX_OUT   := $(OUT)/linux
# LOCALVERSION= + LOCALVERSION_AUTO off (linux.config): release is exactly 6.18.63.
LINUX_FLAGS := -C $(TP)/linux O=$(LINUX_OUT) ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) \
	LOCALVERSION=

$(LINUX_OUT)/.config: configs/linux.config
	mkdir -p $(LINUX_OUT)
	cd $(TP)/linux && ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) \
		scripts/kconfig/merge_config.sh -O $(LINUX_OUT) \
		arch/arm64/configs/defconfig $(CURDIR)/configs/linux.config

linux: $(LINUX_OUT)/.config
	$(MAKE) $(LINUX_FLAGS) -j$(JOBS) Image

################################################################################
# out/bin: everything QEMU needs, under the names the loaders expect
################################################################################
# Only bl1.bin is in flash (-bios). BL1 and BL2 load the other BL images, and
# U-Boot loads Image and rootfs.cpio.gz, from QEMU's working directory via
# semihosting. That is QEMU-only: a real board would read eMMC/SPI flash.
bin: tf-a buildroot linux
	mkdir -p $(BIN)
	ln -sf $(TFA_OUT)/bl1.bin  $(BIN)/bl1.bin
	ln -sf $(TFA_OUT)/bl2.bin  $(BIN)/bl2.bin
	ln -sf $(TFA_OUT)/bl31.bin $(BIN)/bl31.bin
	ln -sf $(OPTEE_OUT)/core/tee-header_v2.bin   $(BIN)/bl32.bin
	ln -sf $(OPTEE_OUT)/core/tee-pager_v2.bin    $(BIN)/bl32_extra1.bin
	ln -sf $(OPTEE_OUT)/core/tee-pageable_v2.bin $(BIN)/bl32_extra2.bin
	ln -sf $(UBOOT_OUT)/u-boot.bin $(BIN)/bl33.bin
	ln -sf $(LINUX_OUT)/arch/arm64/boot/Image $(BIN)/Image
	ln -sf $(BR_OUT)/images/rootfs.cpio.gz $(BIN)/rootfs.cpio.gz

################################################################################
# Run
################################################################################
# secure=on:    EL3 + Secure world exist (TF-A, OP-TEE).
# virtualization=off: no EL2, so BL33 (U-Boot) and Linux run at NS-EL1.
# cortex-a53:   same CPU as Part A (ARMv8.0).
# -semihosting-config target=native: semihosting file I/O on the host (out/bin).
# -nic none:    no network card (not needed; the default virtio NIC also needs
#               an option ROM that is not in the container).
# Serial 0 (UART0 0x09000000): TF-A, U-Boot, Linux -> terminal.
# Serial 1 (UART1 0x09040000, secure-only with secure=on): OP-TEE -> secure.log.
QEMU_FLAGS := -M virt,secure=on,virtualization=off,gic-version=3,acpi=off \
	-cpu cortex-a53 -smp $(SMP) -m 1024 -nographic -nic none \
	-bios bl1.bin -semihosting-config enable=on,target=native \
	-serial mon:stdio -serial file:secure.log

run: bin
	@echo "OP-TEE log: $(BIN)/secure.log   Quit QEMU: Ctrl-A then X"
	cd $(BIN) && qemu-system-aarch64 $(QEMU_FLAGS)

debug: bin
	@echo "Waiting for GDB on :1234 (gdb-multiarch, target remote :1234)"
	cd $(BIN) && qemu-system-aarch64 $(QEMU_FLAGS) -s -S

# Boot-log checks: the same QEMU command, driven by expect.
test: bin
	BIN=$(BIN) QEMU_FLAGS="$(QEMU_FLAGS)" ./scripts/test_b1.sh

clean:
	rm -rf $(OPTEE_OUT) $(UBOOT_OUT) $(OUT)/tf-a $(BR_OUT) $(LINUX_OUT) $(BIN)
