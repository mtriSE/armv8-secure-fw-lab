CROSS   ?= aarch64-linux-gnu-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump

BUILD := build
# -mgeneral-regs-only: no FP/SIMD registers. Variadic functions (printf) would
# otherwise save q0-q7 in their prologue, which faults if FP access is trapped.
CFLAGS  := -mcpu=cortex-a53 -mgeneral-regs-only -ffreestanding -nostdlib -fno-pic -O0 -g -Wall -Wextra -Iinclude
ASFLAGS := -mcpu=cortex-a53 -g -Iinclude
LDFLAGS := -nostdlib -static -Wl,--build-id=none -Wl,--no-warn-rwx-segments

# EL3 firmware: src/ (runs from secure flash, data in secure RAM)
SRCS_C := $(wildcard src/*.c)
SRCS_S := $(wildcard src/*.S)
OBJS   := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS_C)) $(patsubst src/%.S,$(BUILD)/%.o,$(SRCS_S))

# EL1 payload: payload/ plus the shared console/libc-like code from src/.
# A separate image linked at NS RAM, embedded into the firmware (src/payload_blob.S).
PL_BUILD  := $(BUILD)/payload
PL_SHARED := uart printf lib
PL_OBJS   := $(patsubst payload/%.c,$(PL_BUILD)/%.o,$(wildcard payload/*.c)) \
             $(patsubst payload/%.S,$(PL_BUILD)/%.o,$(wildcard payload/*.S)) \
             $(patsubst %,$(PL_BUILD)/%.o,$(PL_SHARED))

QEMU       := qemu-system-aarch64
# -semihosting: the firmware stops QEMU itself when the run is over (src/semihost.c).
# -nic none: no network card (the default one needs the ipxe-qemu option ROM).
QEMU_FLAGS := -M virt,secure=on,gic-version=3 -cpu cortex-a53 -m 256M -nographic -semihosting \
              -nic none -bios $(BUILD)/firmware.bin

.PHONY: all run debug test clean dis dis-payload
all: $(BUILD)/firmware.elf $(BUILD)/firmware.bin

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/%.o: src/%.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@
$(BUILD)/payload_blob.o: $(BUILD)/payload.bin
$(BUILD)/firmware.elf: $(OBJS) linker/link.ld
	$(CC) $(LDFLAGS) -T linker/link.ld $(OBJS) -o $@ -Wl,-Map=$(BUILD)/firmware.map
$(BUILD)/firmware.bin: $(BUILD)/firmware.elf
	$(OBJCOPY) -O binary $< $@

# Payload rules: payload/ sources first, then the shared ones from src/.
$(PL_BUILD)/%.o: payload/%.c | $(PL_BUILD)
	$(CC) $(CFLAGS) -Ipayload -c $< -o $@
$(PL_BUILD)/%.o: payload/%.S | $(PL_BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@
$(PL_BUILD)/%.o: src/%.c | $(PL_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/payload.elf: $(PL_OBJS) payload/link.ld
	$(CC) $(LDFLAGS) -T payload/link.ld $(PL_OBJS) -o $@ -Wl,-Map=$(BUILD)/payload.map
$(BUILD)/payload.bin: $(BUILD)/payload.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD) $(PL_BUILD):
	mkdir -p $@

dis: $(BUILD)/firmware.elf
	$(OBJDUMP) -d $<
dis-payload: $(BUILD)/payload.elf
	$(OBJDUMP) -d $<

run: all
	$(QEMU) $(QEMU_FLAGS)

debug: all
	$(QEMU) $(QEMU_FLAGS) -s -S

# Regression: every finished milestone. Run all, fail if any failed.
test: all
	@rc=0; \
	for t in scripts/test_a[0-9].sh; do $$t || rc=1; done; \
	exit $$rc

clean:
	rm -rf $(BUILD)
