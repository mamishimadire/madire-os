# Madire OS build (runs on Linux: Codespace, WSL, or CI).
#
#   make        build the kernel ELF
#   make iso    build the bootable ISO (build/madire-os.iso)
#   make test   run tests/qemu-tests.sh: Multiboot header, normal boot,
#               logo, and crash-screen tests, all headless in QEMU
#   make run    boot the ISO in QEMU with a window (on Windows use
#               scripts/run.ps1 to boot in VirtualBox instead)
#   make clean  remove build output

# Prefer a dedicated i686-elf cross compiler when installed; otherwise use the
# host GCC in 32-bit freestanding mode.
ifneq ($(shell command -v i686-elf-gcc 2>/dev/null),)
CC := i686-elf-gcc
LD := i686-elf-ld
else
CC := gcc -m32
LD := ld -m elf_i386
endif
AS := nasm

CFLAGS  := -std=gnu11 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -O2 -Wall -Wextra -Werror -I.
ASFLAGS := -f elf32
LDFLAGS := -T linker/linker.ld -nostdlib -z noexecstack

BUILD  := build
KERNEL := $(BUILD)/madire.kernel
ISO    := $(BUILD)/madire-os.iso

# Every .c/.asm under these directories is part of the kernel. boot.o goes
# first only for readability; the linker script places the Multiboot header.
ASM_SRCS := boot/boot.asm $(shell find kernel drivers -name '*.asm')
C_SRCS   := $(shell find kernel drivers -name '*.c')
OBJS     := $(ASM_SRCS:%.asm=$(BUILD)/%.o) $(C_SRCS:%.c=$(BUILD)/%.o)

.PHONY: all iso run test clean

all: $(KERNEL)

$(BUILD)/%.o: %.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(KERNEL): $(OBJS) linker/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

iso: $(ISO)

$(ISO): $(KERNEL) iso/boot/grub/grub.cfg
	rm -rf $(BUILD)/isodir
	mkdir -p $(BUILD)/isodir/boot/grub
	cp $(KERNEL) $(BUILD)/isodir/boot/madire.kernel
	cp iso/boot/grub/grub.cfg $(BUILD)/isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(BUILD)/isodir 2>/dev/null
	@echo "ISO ready: $@"

test: $(ISO)
	bash tests/qemu-tests.sh $(KERNEL) $(ISO)

run: $(ISO)
	qemu-system-i386 -cdrom $(ISO) -serial stdio

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d)
