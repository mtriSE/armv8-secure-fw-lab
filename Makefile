CROSS   ?= aarch64-linux-gnu-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump

BUILD := build
CFLAGS  := -mcpu=cortex-a53 -ffreestanding -nostdlib -fno-pic -O0 -g -Wall -Wextra -Iinclude
ASFLAGS := -mcpu=cortex-a53 -g
LDFLAGS := -nostdlib -static -T linker/link.ld -Wl,--build-id=none -Wl,--no-warn-rwx-segments

SRCS_C := $(wildcard src/*.c)
SRCS_S := $(wildcard src/*.S)
OBJS   := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS_C)) $(patsubst src/%.S,$(BUILD)/%.o,$(SRCS_S))

QEMU       := qemu-system-aarch64
QEMU_FLAGS := -M virt,secure=on,gic-version=3 -cpu cortex-a53 -m 256M -nographic -bios $(BUILD)/firmware.bin

.PHONY: all run debug test clean dis
all: $(BUILD)/firmware.elf $(BUILD)/firmware.bin

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/%.o: src/%.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@
$(BUILD)/firmware.elf: $(OBJS) linker/link.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@ -Wl,-Map=$(BUILD)/firmware.map
$(BUILD)/firmware.bin: $(BUILD)/firmware.elf
	$(OBJCOPY) -O binary $< $@
$(BUILD):
	mkdir -p $@

dis: $(BUILD)/firmware.elf
	$(OBJDUMP) -d $<

run: all
	$(QEMU) $(QEMU_FLAGS)

debug: all
	$(QEMU) $(QEMU_FLAGS) -s -S

test: all
	scripts/test_a1.sh

clean:
	rm -rf $(BUILD)
