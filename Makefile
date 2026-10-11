# Madire OS build (runs on Linux: Codespace, WSL, or CI).
#
#   make        build the kernel (build/madire.elf with symbols, and the
#               bootable flat image build/madire.kernel)
#   make iso    build the bootable ISO (build/madire-os.iso)
#   make unit   host unit tests for the pure kernel modules (tests/unit/)
#   make test   unit tests, then tests/qemu-tests.sh: Multiboot header,
#               normal boot, logo, memory and crash-screen tests in QEMU
#   make test-shard SHARD=<part>   one part of the QEMU tests (0.12)
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
                            kernel/console/textbuf.c kernel/console/lineedit.c kernel/console/outq.c \
                            kernel/shell/parser.c kernel/shell/shell.c kernel/shell/cpuinfo.c \
                            kernel/memory/vmm_core.c kernel/memory/heap.c kernel/bootmod.c \
                            kernel/stackpeak.c kernel/proc/sched_core.c \
                            kernel/console/history.c kernel/rtc_time.c kernel/logbuf.c \
                            kernel/console/scrollback.c kernel/memory/uvm_core.c                             kernel/memory/usercopy_core.c kernel/proc/syscall_core.c                             kernel/proc/proc_core.c kernel/proc/userimg.c \
                            kernel/proc/elf_core.c kernel/proc/progmod_core.c \
                            kernel/proc/loadplan.c kernel/proc/diagwait_core.c)
UNIT_TESTS   := $(patsubst tests/unit/%.c,$(BUILD)/unit/%,$(wildcard tests/unit/test_*.c))

.PHONY: all iso run test test-shard unit user elfcheck repro clean

all: $(KERNEL)

$(BUILD)/%.o: %.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# 0.10 stage 4: the built-in user programs (build/user/images.o, below)
# are linked into the kernel, after its own objects.
USER_IMAGES := $(BUILD)/user/images.o

$(ELF): $(OBJS) $(USER_IMAGES) linker/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(USER_IMAGES)

# The bootable image: the ELF's loaded sections as one flat binary starting
# at 1 MiB. boot/boot.asm's Multiboot header gives the loader its addresses
# (a.out kludge); GRUB and QEMU's -kernel both accept it. The build fails if
# the compiler used any SSE/x87 register.
$(KERNEL): $(ELF)
	@if objdump -d $(ELF) | grep -qE '%(xmm|ymm|zmm|st)'; then \
	    echo "error: SSE/x87 registers used in $(ELF)"; exit 1; fi
	objcopy -O binary $(ELF) $@

iso: $(ISO)

# Built-in user programs (0.10). Each program in user/prog/ is linked with
# user/crt0.asm at fixed addresses (user/link.ld) and turned into raw blobs;
# scripts/user-images.sh checks every program and writes the table of
# images (build/user/images.asm, struct userimg), which the kernel links
# (since 0.10 stage 4).
#   -mgeneral-regs-only  no x87/MMX/SSE: CR0.EM is set and no FPU state is
#                        saved between processes (checked by the script)
USER_CFLAGS := -std=gnu11 -m64 -ffreestanding -fno-builtin -nostdlib -fno-pic -fno-pie                -mgeneral-regs-only -fno-stack-protector -fno-asynchronous-unwind-tables                -fno-tree-loop-distribute-patterns -O2 -Wall -Wextra -Werror -I.
USER_LDFLAGS := -T user/link.ld -nostdlib -z noexecstack -z max-page-size=0x1000 --build-id=none
USER_PROGS  := $(sort $(basename $(notdir $(wildcard user/prog/*.c))))
USER_ELFS   := $(USER_PROGS:%=$(BUILD)/user/%.elf)

$(BUILD)/user/crt0.o: user/crt0.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/user/%.o: user/prog/%.c user/syscall.h user/ulib.h
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD)/user/%.elf: $(BUILD)/user/crt0.o $(BUILD)/user/%.o user/link.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(BUILD)/user/crt0.o $(BUILD)/user/$*.o

$(BUILD)/user/images.asm: $(USER_ELFS) scripts/user-images.sh
	sh scripts/user-images.sh $(BUILD)/user $(USER_PROGS) > $@.tmp
	mv $@.tmp $@

$(BUILD)/user/images.o: $(BUILD)/user/images.asm
	$(AS) $(ASFLAGS) $< -o $@

user: $(BUILD)/user/images.o $(BUILD)/test-user/flood.elf

# Test-only ELF for the stage 5 queue back-pressure test. It stays outside
# build/user so it cannot change the built-in image or ISO module lists.
$(BUILD)/test-user/flood.o: tests/user/flood.c user/syscall.h user/ulib.h
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD)/test-user/flood.elf: $(BUILD)/user/crt0.o $(BUILD)/test-user/flood.o user/link.ld
	$(LD) $(USER_LDFLAGS) -o $@ $(BUILD)/user/crt0.o $(BUILD)/test-user/flood.o

# Keep the programs' objects (make would delete them as intermediates).
.SECONDARY: $(BUILD)/user/crt0.o $(USER_PROGS:%=$(BUILD)/user/%.o)

# The ISO build also runs the source checks (0.9: boot screen usage).
# 0.11: the programs the default entry loads as modules are the ELF files
# named in grub.cfg's "module /boot/prog/<name>.elf" lines (one list only).
ISO_PROGS := $(sort $(patsubst /boot/prog/%.elf,%,$(shell if test -f iso/boot/grub/grub.cfg; then grep -o '/boot/prog/[a-z0-9_]*\.elf' iso/boot/grub/grub.cfg; fi)))
# 0.12: one version source. grub.cfg's default entry is titled
# "Madire OS v@MADIRE_OS_VERSION@"; the build fills in MADIRE_OS_VERSION from
# kernel/kernel.h, and the ISO and the tests use the generated file.
MADIRE_VERSION := $(shell sed -n 's/^#define MADIRE_OS_VERSION "\([0-9.]*\)"$$/\1/p' kernel/kernel.h)
$(BUILD)/grub.cfg: iso/boot/grub/grub.cfg kernel/kernel.h
	@mkdir -p $(BUILD)
	@test -n "$(MADIRE_VERSION)" || { echo "cannot read MADIRE_OS_VERSION from kernel/kernel.h"; exit 1; }
	sed 's/@MADIRE_OS_VERSION@/$(MADIRE_VERSION)/g' iso/boot/grub/grub.cfg > $@
	@! grep -q '@MADIRE_OS_VERSION@' $@

$(ISO): $(KERNEL) $(BUILD)/grub.cfg $(ISO_PROGS:%=$(BUILD)/user/%.elf)
	sh scripts/check-bootscreen.sh
	rm -rf $(BUILD)/isodir
	mkdir -p $(BUILD)/isodir/boot/grub $(BUILD)/isodir/boot/prog
	cp $(KERNEL) $(BUILD)/isodir/boot/madire.kernel
	cp $(BUILD)/grub.cfg $(BUILD)/isodir/boot/grub/grub.cfg
	for p in $(ISO_PROGS); do cp $(BUILD)/user/$$p.elf $(BUILD)/isodir/boot/prog/$$p.elf || exit 1; done
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

# 0.11: the kernel's ELF checks (kernel/proc/elf_core.c) as a host tool, for
# the built programs and the malformed fixtures (tests/elf-fixtures.py);
# tests/qemu-tests.sh runs it.
ELFCHECK := $(BUILD)/elfcheck
$(ELFCHECK): tests/elfcheck.c kernel/proc/elf_core.c kernel/proc/elf_core.h
	@mkdir -p $(dir $@)
	$(UNIT_CC) $(UNIT_CFLAGS) tests/elfcheck.c kernel/proc/elf_core.c -o $@

elfcheck: $(ELFCHECK)

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
	find Makefile boot kernel drivers linker user scripts/user-images.sh -type f | LC_ALL=C sort > $(REPRO)/files
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

test: unit user elfcheck $(ISO) $(BUILD)/grub.cfg repro
	bash tests/qemu-tests.sh $(KERNEL) $(ISO)

# 0.12 (stage 0c): one part of the QEMU tests (make test-shard SHARD=<part>;
# the parts: bash tests/qemu-tests.sh --list-parts), for the parallel jobs
# of .github/workflows/regression.yml. The unit tests and make repro run
# only with SHARD=static, so a sharded run runs them once.
SHARD ?= static
test-shard: user elfcheck $(ISO) $(BUILD)/grub.cfg $(if $(filter static,$(SHARD)),unit repro)
	bash tests/qemu-tests.sh $(KERNEL) $(ISO) --shard $(SHARD)

run: $(ISO)
	qemu-system-x86_64 -cdrom $(ISO) -serial stdio

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d)
