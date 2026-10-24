# NOVA OS — top-level build
#
# Produces build/nova.img, a raw BIOS-bootable disk image containing:
#   LBA 0        our MBR stage 1
#   LBA 1..60    our stage 2 loader
#   LBA 64..     flat kernel image (higher-half ELF converted to binary)
#   LBA 16384..  NOVA FS partition (type 0x7F) with the system tree
#
# No Linux/BSD code, no GRUB, no external bootloader.

CC      := gcc
LD      := ld
OBJCOPY := objcopy
NM      := nm
PYTHON  := python3

BUILD   := build
TOOLCHAIN := $(CURDIR)/.toolchain
QEMU    := $(TOOLCHAIN)/bin/qemu-system-x86_64
NASM    := $(TOOLCHAIN)/bin/nasm
MINGW   := $(TOOLCHAIN)/bin/x86_64-w64-mingw32-gcc
MINGW32 := $(TOOLCHAIN)/bin/i686-w64-mingw32-gcc

KERNEL_VIRT_BASE := 0xFFFFFFFF80100000
KERNEL_PHYS_BASE := 0x100000

INCLUDES := -Ikernel/include -Ikernel -I$(BUILD)

CWARN := -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
         -Wno-missing-field-initializers
CFLAGS := -std=gnu11 -O2 -g $(CWARN) $(INCLUDES) \
          -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
          -mno-red-zone -mcmodel=kernel -mno-mmx -mno-sse -mno-sse2 \
          -fno-builtin -fno-asynchronous-unwind-tables -fno-unwind-tables \
          -fno-omit-frame-pointer -mgeneral-regs-only
ASFLAGS := -m64 -g -Ikernel/include -I$(BUILD)
LDFLAGS := -m elf_x86_64 -nostdlib -z max-page-size=0x1000

C_SRCS  := $(shell find kernel -name '*.c' | sort)
AS_SRCS := $(shell find kernel -name '*.S' | sort)
OBJS    := $(patsubst %.c,$(BUILD)/%.o,$(C_SRCS)) \
           $(patsubst %.S,$(BUILD)/%.o,$(AS_SRCS))

KERNEL_ELF := $(BUILD)/kernel.elf
KERNEL_BIN := $(BUILD)/kernel.bin
IMAGE      := $(BUILD)/nova.img

.PHONY: all kernel image run run-headless test clean dirs userland font tools-check

all: image

dirs:
	@mkdir -p $(BUILD)

# ---------------------------------------------------------------- kernel ---
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	@$(CC) $(ASFLAGS) -c $< -o $@

kernel: $(KERNEL_ELF)

$(KERNEL_ELF): $(OBJS) kernel/kernel.ld
	@mkdir -p $(BUILD)
	$(LD) $(LDFLAGS) -T kernel/kernel.ld -o $@ $(OBJS) -Map $(BUILD)/kernel.map
	@$(OBJCOPY) -O binary $@ $(KERNEL_BIN)
	@python3 tools/mkimage.py $(KERNEL_BIN) $(BUILD)/kernel_sectors.inc
	@echo "kernel: $$(stat -c%s $(KERNEL_BIN)) bytes, $$(grep -o '[0-9]*' $(BUILD)/kernel_sectors.inc | tail -1) sectors"

# -------------------------------------------------------------- bootloader --
$(BUILD)/stage1.bin: boot/stage1.S boot/stage1.ld | dirs
	@mkdir -p $(BUILD)
	$(CC) -c -m64 -nostdlib boot/stage1.S -o $(BUILD)/stage1.o
	$(LD) -T boot/stage1.ld --oformat binary -o $@ $(BUILD)/stage1.o
	@test $$(stat -c%s $@) -eq 512 || (echo "stage1 must be exactly 512 bytes" && false)

$(BUILD)/stage2.bin: boot/stage2.S boot/stage2.ld $(BUILD)/kernel_sectors.inc | dirs
	@mkdir -p $(BUILD)
	$(CC) -c -m64 -nostdlib -I$(BUILD) boot/stage2.S -o $(BUILD)/stage2.o
	$(LD) -T boot/stage2.ld --oformat binary -o $@ $(BUILD)/stage2.o
	@test $$(stat -c%s $@) -le $$((60*512)) || (echo "stage2 too large" && false)
	@echo "stage2: $$(stat -c%s $@) bytes"

# ------------------------------------------------------------------ userland --
userland: kernel $(BUILD)/stage1.bin $(BUILD)/stage2.bin
	@./tools/build-userland.sh

$(BUILD)/stage2.bin $(BUILD)/stage1.bin: | kernel

# ------------------------------------------------------------------- image --
image: kernel $(BUILD)/stage1.bin $(BUILD)/stage2.bin userland
	@python3 tools/mkdisk.py --stage1 $(BUILD)/stage1.bin \
		--stage2 $(BUILD)/stage2.bin --kernel $(KERNEL_BIN) \
		--userland $(BUILD)/userland --out $(IMAGE)
	@echo "image: $(IMAGE) ($$(stat -c%s $(IMAGE)) bytes)"

run: image
	$(QEMU) -drive format=raw,file=$(IMAGE),if=ide \
		-m 256M -serial stdio -display gtk -vga std \
		-machine pc -rtc base=localtime

run-headless: image
	$(QEMU) -drive format=raw,file=$(IMAGE),if=ide \
		-m 256M -serial stdio -display none -vga std -no-reboot \
		-machine pc -d int -D $(BUILD)/qemu.log

# -------------------------------------------------------------------- tests --
test: | dirs
	@./tests/run.sh

clean:
	rm -rf $(BUILD)
