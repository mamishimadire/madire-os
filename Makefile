# Madire OS build (runs on Linux: Codespace, WSL, or CI).
#
#   make        build the kernel ELF
#   make iso    build the bootable ISO (build/madire-os.iso)
#   make test   verify the Multiboot header, then boot the ISO in QEMU and
#               check the serial output for "System ready."
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
           -O2 -Wall -Wextra -Werror -Iinclude -Ikernel
ASFLAGS := -f elf32
LDFLAGS := -T linker/linker.ld -nostdlib -z noexecstack

BUILD  := build
KERNEL := $(BUILD)/madire.kernel
ISO    := $(BUILD)/madire-os.iso
OBJS   := $(BUILD)/boot/boot.o $(BUILD)/kernel/kernel.o

.PHONY: all iso run test clean

all: $(KERNEL)

$(BUILD)/boot/%.o: boot/%.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/kernel/%.o: kernel/%.c kernel/kernel.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

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
	grub-file --is-x86-multiboot $(KERNEL) && echo "PASS: kernel has a valid Multiboot header"
	@timeout 15 qemu-system-i386 -cdrom $(ISO) -display none -serial file:$(BUILD)/serial.log \
		-no-reboot || true
	@grep -q "System ready." $(BUILD)/serial.log \
		&& echo "PASS: kernel booted in QEMU and printed 'System ready.'" \
		|| (echo "FAIL: no 'System ready.' in serial output:"; cat $(BUILD)/serial.log; exit 1)

run: $(ISO)
	qemu-system-i386 -cdrom $(ISO) -serial stdio

clean:
	rm -rf $(BUILD)
