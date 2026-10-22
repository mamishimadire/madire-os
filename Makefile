# Madire OS build (runs on Linux: Codespace, WSL, or CI).
#
#   make        build the kernel (build/madire.elf with symbols, and the
#               bootable flat image build/madire.kernel)
#   make iso    build the bootable ISO (build/madire-os.iso, booted by Limine since 0.13)
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

# 1.0 stage 1 (D1): a test-only kernel padded past 2 MiB. KERNEL_PAD=<n>[K|M]
# links one more object, a .bss pad of that size, so the image ends that
# much higher. It is never the shipped kernel: it builds only into its own
# BUILD directory (make padkernel: build/pad), never into build/, so the
# ISO and make repro (which build without KERNEL_PAD) never see it.
ifneq ($(KERNEL_PAD),)
ifeq ($(BUILD),build)
$(error KERNEL_PAD builds a test-only kernel: give it its own BUILD directory (make padkernel builds build/pad))
endif
KPAD_BYTES := $(shell printf '%s' '$(KERNEL_PAD)' | sed 's/[Mm]$$/*1048576/; s/[Kk]$$/*1024/')
OBJS += $(BUILD)/kpad.o
.DEFAULT_GOAL := all
$(BUILD)/kpad.o:
	@mkdir -p $(dir $@)
	printf 'section .bss\nalign 4096\nkernel_test_pad: resb %s\n' '$(KPAD_BYTES)' > $(BUILD)/kpad.asm
	$(AS) $(ASFLAGS) $(BUILD)/kpad.asm -o $@
endif

# 1.2 stage 3: the probe kernel. PROBE=1 assembles boot/boot.asm with
# -DPROBE (a video request in the Multiboot header and the probe's report,
# docs/plans/1.2.md stage 3). Like KERNEL_PAD it builds only into its own
# BUILD directory (make probe: build/probe), so the real kernel, the ISO and
# make repro never see it.
ifneq ($(PROBE),)
ifeq ($(BUILD),build)
$(error PROBE builds a test-only kernel: give it its own BUILD directory (make probe builds build/probe))
endif
ASFLAGS += -DPROBE
endif

# Host unit tests: kernel modules with no hardware access, built as ordinary
# 64-bit Linux programs (0.7: the kernel's integer and pointer sizes).
# Non-PIE and linked at 256 MiB: the memory tests pass the addresses of their
# static test data as 32-bit physical addresses, so that data must lie below
# 4 GiB; test_reserve also expects it above its fake 32 MiB machine.
UNIT_CC      := gcc -m64 -no-pie -Wl,-Ttext-segment=0x10000000
UNIT_CFLAGS  := -std=gnu11 -O2 -Wall -Wextra -Werror -fno-pie -I.
UNIT_MODULES := $(wildcard kernel/memory/mmap.c kernel/memory/pmm.c kernel/memory/reserve.c \
                            drivers/keyboard/scancode.c drivers/keyboard/ps2leds.c kernel/input/keyqueue.c \
                            kernel/console/textbuf.c kernel/console/lineedit.c kernel/console/outq.c \
                            kernel/shell/parser.c kernel/shell/shell.c kernel/shell/cpuinfo.c \
                            kernel/memory/vmm_core.c kernel/memory/heap.c kernel/bootmod.c \
                            kernel/stackpeak.c kernel/proc/sched_core.c \
                            kernel/console/history.c kernel/rtc_time.c kernel/logbuf.c \
                            kernel/console/scrollback.c kernel/memory/uvm_core.c                             kernel/memory/usercopy_core.c kernel/proc/syscall_core.c                             kernel/proc/proc_core.c kernel/proc/userimg.c \
                            kernel/proc/elf_core.c kernel/proc/progmod_core.c \
                            kernel/proc/loadplan.c kernel/proc/diagwait_core.c \
                            kernel/fs/path_core.c kernel/fs/vfs_core.c kernel/fs/ramfs_core.c \
                            kernel/proc/klock_core.c kernel/proc/fdtable_core.c \
                            kernel/fs/fsys_core.c)
UNIT_TESTS   := $(patsubst tests/unit/%.c,$(BUILD)/unit/%,$(wildcard tests/unit/test_*.c))

.PHONY: all iso run test test-shard unit user elfcheck repro padkernel probe probe-iso clean

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
# (a.out kludge); Limine and QEMU's -kernel both accept it. The build fails if
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

# 1.0 stage 7: args enters at its own stub (args_entry, user/prog/args.c),
# which records the registers at entry and jumps to crt0's _start.
$(BUILD)/user/args.elf: $(BUILD)/user/crt0.o $(BUILD)/user/args.o user/link.ld
	$(LD) $(USER_LDFLAGS) -e args_entry -o $@ $(BUILD)/user/crt0.o $(BUILD)/user/args.o

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
# 0.13: the ISO boots through Limine (the separate boot tool, AGENTS.md
# section 11, item 5). The programs the default entry loads as modules are
# the ELF files named in limine.conf's "module_path: boot():/boot/prog/<name>.elf"
# lines (one list only).
ISO_PROGS := $(sort $(patsubst boot():/boot/prog/%.elf,%,$(shell if test -f iso/boot/limine/limine.conf; then grep -o 'boot():/boot/prog/[a-z0-9_]*\.elf' iso/boot/limine/limine.conf; fi)))
# 0.12: one version source. The build fills in @MADIRE_OS_VERSION@ in
# limine.conf from kernel/kernel.h, and the ISO and the tests use the
# generated file. 1.1: the default entry's title no longer carries the
# version; the boot menu's wallpaper shows it (scripts/make-wallpaper.py,
# given the same version).
MADIRE_VERSION := $(shell sed -n 's/^#define MADIRE_OS_VERSION "\([0-9.]*\)"$$/\1/p' kernel/kernel.h)
$(BUILD)/limine.conf: iso/boot/limine/limine.conf kernel/kernel.h
	@mkdir -p $(BUILD)
	@test -n "$(MADIRE_VERSION)" || { echo "cannot read MADIRE_OS_VERSION from kernel/kernel.h"; exit 1; }
	sed 's/@MADIRE_OS_VERSION@/$(MADIRE_VERSION)/g' iso/boot/limine/limine.conf > $@
	@! grep -q '@MADIRE_OS_VERSION@' $@

# 1.1: the boot menu's wallpaper (1024x768 PNG, Python standard library
# only), with the M of kernel/logo.c and the version from kernel/kernel.h.
$(BUILD)/wallpaper.png: scripts/make-wallpaper.py kernel/kernel.h kernel/logo.c
	@mkdir -p $(BUILD)
	@test -n "$(MADIRE_VERSION)" || { echo "cannot read MADIRE_OS_VERSION from kernel/kernel.h"; exit 1; }
	python3 scripts/make-wallpaper.py $@ --version $(MADIRE_VERSION)

# 0.13: LIMINE_DIR holds the pinned package's BIOS files and notices, LIMINE
# is its host tool (scripts/install-deps.sh installs both). "limine
# bios-install" (which also makes the image bootable as a disk) needs an MBR
# whose partition starts at sector 63 or later: --protective-msdos-label with
# -partition_offset 16 gives one starting at sector 64. SOURCE_DATE_EPOCH
# (fixed, 2026-10-12 00:00 UTC) was the 0.13 attempt (D5) at a reproducible
# ISO; the ISO still differs between builds (a watch item, docs/PROGRESS.md).
# 1.0 (stage 0a): Madire's own LICENSE, NOTICE and THIRD_PARTY_NOTICES go to
# the ISO root (Apache 2.0, section 4); Limine's stay in /boot/limine/.
# 1.1: the boot menu's wallpaper goes to /boot/limine/wallpaper.png.
LIMINE_DIR ?= /usr/local/share/limine
LIMINE ?= limine
ISO_EPOCH := 1791763200
$(ISO): $(KERNEL) $(BUILD)/limine.conf $(BUILD)/wallpaper.png $(ISO_PROGS:%=$(BUILD)/user/%.elf) LICENSE NOTICE THIRD_PARTY_NOTICES
	sh scripts/check-bootscreen.sh
	rm -rf $(BUILD)/isodir
	mkdir -p $(BUILD)/isodir/boot/limine $(BUILD)/isodir/boot/prog
	cp LICENSE NOTICE THIRD_PARTY_NOTICES $(BUILD)/isodir/
	cp $(KERNEL) $(BUILD)/isodir/boot/madire.kernel
	cp $(BUILD)/limine.conf $(BUILD)/isodir/boot/limine/limine.conf
	cp $(BUILD)/wallpaper.png $(BUILD)/isodir/boot/limine/wallpaper.png
	cp $(LIMINE_DIR)/limine-bios.sys $(LIMINE_DIR)/limine-bios-cd.bin $(LIMINE_DIR)/LICENSE \
	    $(LIMINE_DIR)/3RDPARTY.md $(BUILD)/isodir/boot/limine/
	for p in $(ISO_PROGS); do cp $(BUILD)/user/$$p.elf $(BUILD)/isodir/boot/prog/$$p.elf || exit 1; done
	SOURCE_DATE_EPOCH=$(ISO_EPOCH) xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
	    -no-emul-boot -boot-load-size 4 -boot-info-table --protective-msdos-label -partition_offset 16 \
	    -o $@ $(BUILD)/isodir 2>/dev/null
	$(LIMINE) bios-install $@
	@echo "ISO ready: $@"

$(BUILD)/unit/%: tests/unit/%.c tests/unit/unit.h $(UNIT_MODULES) $(wildcard kernel/memory/*.h) kernel/multiboot.h \
                 kernel/bootmod.h kernel/stackpeak.h kernel/rtc_time.h kernel/logbuf.h kernel/kprintf.c kernel/kprintf.h $(wildcard kernel/arch/x86/*.h) \
                 $(wildcard drivers/keyboard/*.h drivers/pit/*.h kernel/input/*.h kernel/console/*.h kernel/shell/*.h) \
                 $(wildcard kernel/proc/*.h kernel/fs/*.h)
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
# The ISO is not covered: it is not reproducible (see the watch items in
# docs/PROGRESS.md).
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

# 1.0 stage 1 (D1): the padded test kernel (2 MiB of .bss more since stage 5:
# the kernel itself is past 2 MiB, so the pad takes it past 4 MiB) in build/pad,
# for the [1.0] checks in tests/qemu-part-4.sh. A failed build is reported
# and leaves no kernel there, so those checks fail without stopping the run.
PAD_BUILD := $(BUILD)/pad
padkernel:
	@echo "KERNEL_PAD_REFUSAL (make -n KERNEL_PAD=1, the default BUILD; a dry run): $$($(MAKE) -n KERNEL_PAD=1 BUILD=build all 2>&1 | grep -m1 'KERNEL_PAD' || echo 'NOT REFUSED')"
	@rm -rf $(PAD_BUILD)
	@start=$$(date +%s); \
	if $(MAKE) -s BUILD=$(PAD_BUILD) KERNEL_PAD=2M all > $(BUILD)/padkernel.log 2>&1; \
	then echo "PADKERNEL_BUILD_SECONDS=$$(( $$(date +%s) - start ))"; \
	else echo "PADKERNEL_BUILD=FAILED (see $(BUILD)/padkernel.log)"; rm -f $(PAD_BUILD)/madire.kernel; fi

# 1.2 stage 3: the probe kernel in build/probe, for the report-only probe
# section of tests/qemu-part-1.sh; probe-iso also makes the probe ISO for
# the VirtualBox probe (scripts/vbox-probe.ps1; never acceptance evidence).
# A failed build is reported and leaves no kernel there.
PROBE_BUILD := $(BUILD)/probe
probe:
	@echo "PROBE_REFUSAL (make -n PROBE=1, the default BUILD; a dry run): $$($(MAKE) -n PROBE=1 BUILD=build all 2>&1 | grep -m1 'PROBE' || echo 'NOT REFUSED')"
	@rm -rf $(PROBE_BUILD)
	@start=$$(date +%s); \
	if $(MAKE) -s BUILD=$(PROBE_BUILD) PROBE=1 all > $(BUILD)/probe.log 2>&1; \
	then echo "PROBE_BUILD_SECONDS=$$(( $$(date +%s) - start ))"; \
	else echo "PROBE_BUILD=FAILED (see $(BUILD)/probe.log)"; rm -f $(PROBE_BUILD)/madire.kernel; fi

probe-iso: probe $(BUILD)/limine.conf $(BUILD)/wallpaper.png
	sh tests/probe-iso.sh $(BUILD) 1 $(PROBE_BUILD)/madire-probe.iso
	python3 scripts/make-wallpaper.py $(PROBE_BUILD)/wallpaper-layout.png --version $(MADIRE_VERSION) \
	    --layout-json $(PROBE_BUILD)/layout.json
	sha256sum $(PROBE_BUILD)/madire-probe.iso $(PROBE_BUILD)/madire.kernel $(BUILD)/wallpaper.png

test: unit user elfcheck $(ISO) $(BUILD)/limine.conf $(BUILD)/wallpaper.png repro padkernel probe
	bash tests/qemu-tests.sh $(KERNEL) $(ISO)

# 0.12 (stage 0c): one part of the QEMU tests (make test-shard SHARD=<part>;
# the parts: bash tests/qemu-tests.sh --list-parts), for the parallel jobs
# of .github/workflows/regression.yml. The unit tests and make repro run
# only with SHARD=static, so a sharded run runs them once; the padded test
# kernel (1.0) is built only for part 4, which boots it; the probe kernel
# (1.2 stage 3) only for part 1.
SHARD ?= static
test-shard: user elfcheck $(ISO) $(BUILD)/limine.conf $(BUILD)/wallpaper.png $(if $(filter static,$(SHARD)),unit repro) \
            $(if $(filter 4,$(SHARD)),padkernel) $(if $(filter 1,$(SHARD)),probe)
	bash tests/qemu-tests.sh $(KERNEL) $(ISO) --shard $(SHARD)

run: $(ISO)
	qemu-system-x86_64 -cdrom $(ISO) -serial stdio

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d)
