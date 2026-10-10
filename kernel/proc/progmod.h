#ifndef MADIRE_PROC_PROGMOD_H
#define MADIRE_PROC_PROGMOD_H

/*
 * Program modules (0.11), the kernel side of progmod_core.h. At boot,
 * check_boot_modules (kernel.c) hands every boot module that is not a test
 * module to progmod_register: its name is taken from the command line, its
 * bytes (boot data, read through the physical view) are checked with
 * elf_check, and it is kept or refused. Each decision is one log line,
 * serial and dmesg only (never the screen):
 *   Program module 3 'hello': 5176 bytes, ELF checked: 2 segments, 3 pages, entry 0x400000
 *   Program module 4 'badmagic': 5176 bytes, refused: not an ELF file
 *   Program module 5: refused: bad name ('/boot/x.elf Bad-Name')
 * Stage 2a: nothing is loaded or run yet.
 */

#include <stdint.h>
#include "kernel/proc/progmod_core.h"

/* cmdline: the module's command line (may be NULL); bytes: its contents. */
void progmod_register(uint32_t module, const char *cmdline, uint64_t phys,
                      const uint8_t *bytes, uint64_t size);

/* After the last module: "Program modules: A accepted, R refused (at most 8)"
 * (only if there was a program candidate). */
void progmod_summary(void);

const struct progmod_table *progmod_table(void);

/* 0.11 stage 3: every candidate seen at boot, kept or refused, in module
 * order (at most PROGMOD_CANDIDATES_MAX; a boot takes at most 13 modules),
 * so test=elfrun can try to load even the refused ones and prove that the
 * load checks the file again. */
#define PROGMOD_CANDIDATES_MAX 16u

struct progmod_candidate {
    uint32_t module;
    char name[PROGMOD_NAME_MAX];        /* "" when the name itself was refused */
    const uint8_t *bytes;
    uint64_t size;
    bool accepted;
    enum elf_result elf;                /* the boot check's result */
};

const struct progmod_candidate *progmod_candidates(uint32_t *count);

#endif
