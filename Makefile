# Madire OS build (runs on Linux: Codespace, WSL, or CI).
#
#   make        build the kernel (build/madire.elf with symbols, and the
#               bootable flat image build/madire.kernel)
#   make iso    build the bootable ISO (build/madire-os.iso)
#   make unit   host unit tests for the pure kernel modules (tests/unit/)
#   make test   unit tests, then tests/qemu-tests.sh: Multiboot header,
#               normal boot, logo, memory and crash-screen tests in QEMU
#   make run    boot the ISO in QEMU with a window (on Windows use
#               scripts/run.ps1 to boot in VirtualBox instead)
#   make clean  remove build output

# x86-64 kernel, built with the host GCC in freestanding mode (0.7).
#   -mno-red-zone        interrupts push onto the stack below RSP, so the
#                        128-byte red zone would be overwritten
#   -mgeneral-regs-only  no SSE/MMX/x87/AVX in kernel code (checked below;
#                        CR4.OSFXSR also stays clear, so any SSE faults)
#   -mcmodel=kernel      the kernel is linked in the top 2 GiB (0.8: higher
#                        half, 0xFFFFFFFF80000000 + physical)
# libgcc is not linked: 64-bit division is a native instruction here, and
# 128-bit arithmetic is not used.
CC := gcc
LD := ld -m elf_x86_64
AS := nasm

CFLAGS  := -std=gnu11 -m64 -ffreestanding -fno-pic -fno-pie -mno-red-zone -mgeneral-regs-only \
           -mcmodel=kernel -fno-stack-protector -fno-asynchronous-unwind-tables \
           -fno-tree-loop-distribute-patterns -O2 -Wall -Wextra -Werror -I.
ASFLAGS := -f elf64
LDFLAGS := -T linker/linker.ld -nostdlib -z noexecstack -z max-page-size=0x1000

BUILD  := build
ELF    := $(BUILD)/madire.elf
KERNEL := $(BUILD)/madire.kernel
ISO    := $(BUILD)/madire-os.iso

# Every .c/.asm under these directories is part of the kernel. boot.o goes
# first only for readability; the linker script places the Multiboot header.
# The lists are sorted (0.8): the link order, and so the binary, must not
# depend on the order in which the file system happens to list the files.
ASM_SRCS := boot/boot.asm $(sort $(shell find kernel drivers -name '*.asm'))
C_SRCS   := $(sort $(shell find kernel drivers -name '*.c'))
OBJS     := $(ASM_SRCS:%.asm=$(BUILD)/%.o) $(C_SRCS:%.c=$(BUILD)/%.o)

# Host unit tests: kernel modules with no hardware access, built as ordinary
# 64-bit Linux programs (0.7: the kernel's integer and pointer sizes).
# Non-PIE and linked at 256 MiB: the memory tests pass the addresses of their
# static test data as 32-bit physical addresses, so that data must lie below
# 4 GiB; test_reserve also expects it above its fake 32 MiB machine.
UNIT_CC      := gcc -m64 -no-pie -Wl,-Ttext-segment=0x10000000
UNIT_CFLAGS  := -std=gnu11 -O2 -Wall -Wextra -Werror -fno-pie -I.
UNIT_MODULES := $(wildcard kernel/memory/mmap.c kernel/memory/pmm.c kernel/memory/reserve.c \
                            drivers/keyboard/scancode.c kernel/input/keyqueue.c \
                            kernel/console/textbuf.c kernel/console/lineedit.c \
                            kernel/shell/parser.c kernel/shell/shell.c kernel/shell/cpuinfo.c \
                            kernel/memory/vmm_core.c kernel/memory/heap.c kernel/bootmod.c \
                            kernel/stackpeak.c kernel/proc/sched_core.c \
                            kernel/console/history.c kernel/rtc_time.c kernel/logbuf.c)
UNIT_TESTS   := $(patsubst tests/unit/%.c,$(BUILD)/unit/%,$(wildcard tests/unit/test_*.c))

.PHONY: all iso run test unit repro clean

all: $(KERNEL)

$(BUILD)/%.o: %.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(ELF): $(OBJS) linker/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

# The bootable image: the ELF's loaded sections as one flat binary starting
# at 1 MiB. boot/boot.asm's Multiboot header gives the loader its addresses
# (a.out kludge); GRUB and QEMU's -kernel both accept it. The build fails if
# the compiler used any SSE/x87 register.
$(KERNEL): $(ELF)
	@if objdump -d $(ELF) | grep -qE '%(xmm|ymm|zmm|st)'; then \
	    echo "error: SSE/x87 registers used in $(ELF)"; exit 1; fi
	objcopy -O binary $(ELF) $@

iso: $(ISO)

# The ISO build also runs the source checks (0.9: boot screen usage).
$(ISO): $(KERNEL) iso/boot/grub/grub.cfg
	sh scripts/check-bootscreen.sh
	rm -rf $(BUILD)/isodir
	mkdir -p $(BUILD)/isodir/boot/grub
	cp $(KERNEL) $(BUILD)/isodir/boot/madire.kernel
	cp iso/boot/grub/grub.cfg $(BUILD)/isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(BUILD)/isodir 2>/dev/null
	@echo "ISO ready: $@"

$(BUILD)/unit/%: tests/unit/%.c tests/unit/unit.h $(UNIT_MODULES) $(wildcard kernel/memory/*.h) kernel/multiboot.h \
                 kernel/bootmod.h kernel/stackpeak.h kernel/rtc_time.h kernel/logbuf.h kernel/kprintf.c kernel/kprintf.h $(wildcard kernel/arch/x86/*.h) \
                 $(wildcard drivers/keyboard/*.h drivers/pit/*.h kernel/input/*.h kernel/console/*.h kernel/shell/*.h) \
                 $(wildcard kernel/proc/*.h)
	@mkdir -p $(dir $@)
	$(UNIT_CC) $(UNIT_CFLAGS) $< $(UNIT_MODULES) -o $@

unit: $(UNIT_TESTS)
	@for t in $(UNIT_TESTS); do $$t || exit 1; done

# Reproducible build (0.8): copy the sources into two fresh trees and build
# each from clean. Tree b is built with a `find` that lists files in reverse
# order, so a link order that followed the file system would show up as a
# different binary. madire.elf and madire.kernel must be byte-identical in
# both trees, and to the kernel of this build (the one the QEMU tests boot).
# The ISO is not covered: grub-mkrescue records timestamps.
# tests/qemu-tests.sh reports the result.
REPRO := $(BUILD)/repro
repro: $(KERNEL)
	rm -rf $(REPRO) && mkdir -p $(REPRO)/a $(REPRO)/b $(REPRO)/bin
	find Makefile boot kernel drivers linker -type f | LC_ALL=C sort > $(REPRO)/files
	tar -cf - -T $(REPRO)/files | tar -xf - -C $(REPRO)/a
	tar -cf - -T $(REPRO)/files | tar -xf - -C $(REPRO)/b
	printf '#!/bin/sh\n%s "$$@" | LC_ALL=C sort -r\n' "$$(command -v find)" > $(REPRO)/bin/find
	chmod +x $(REPRO)/bin/find
	$(MAKE) -s -C $(REPRO)/a all >/dev/null
	PATH="$(abspath $(REPRO)/bin):$$PATH" $(MAKE) -s -C $(REPRO)/b all >/dev/null
	cd $(REPRO) && sha256sum a/build/madire.elf b/build/madire.elf a/build/madire.kernel \
	    b/build/madire.kernel ../madire.kernel > sha256.txt
	cmp $(REPRO)/a/build/madire.elf $(REPRO)/b/build/madire.elf && \
	cmp $(REPRO)/a/build/madire.kernel $(REPRO)/b/build/madire.kernel && \
	cmp $(REPRO)/a/build/madire.kernel $(KERNEL) && \
	echo "identical" > $(REPRO)/result || echo "different" > $(REPRO)/result
	@echo "Reproducible build: $$(cat $(REPRO)/result)"

test: unit $(ISO) repro
	bash tests/qemu-tests.sh $(KERNEL) $(ISO)

run: $(ISO)
	qemu-system-x86_64 -cdrom $(ISO) -serial stdio

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d)
