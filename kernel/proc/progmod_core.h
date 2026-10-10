#ifndef MADIRE_PROC_PROGMOD_CORE_H
#define MADIRE_PROC_PROGMOD_CORE_H

/*
 * Program modules (0.11), pure: tests/unit/test_progmod.c. A boot module
 * that is not a test module (kernel/bootmod.h) is a program candidate. Its
 * name comes from the module's command line:
 *   "path"        the path's last part without ".elf"
 *                 ("/boot/prog/hello.elf" -> "hello")
 *   "path name"   the second word
 * A name is 1 to 15 characters of [a-z0-9_]; anything else (no command
 * line, more than two words, an empty or bad name) is refused, and so is
 * a second module with a name already taken. At most PROGMOD_MAX programs
 * are kept; the kernel side (progmod.c) checks each file with elf_check
 * before adding it, and logs every decision.
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/proc/elf_core.h"

#define PROGMOD_MAX       8u
#define PROGMOD_NAME_MAX  16u           /* including the terminating 0 */
#define PROGMOD_SHOW_MAX  40u           /* characters of a bad command line shown */

struct progmod {
    char name[PROGMOD_NAME_MAX];
    uint32_t module;                    /* its Multiboot module number, from 1 */
    uint64_t phys, size;                /* where its bytes are (boot data) */
    struct elf_plan plan;
};

struct progmod_table {
    uint32_t count;
    struct progmod e[PROGMOD_MAX];
};

enum progmod_result {
    PROGMOD_OK,
    PROGMOD_ERR_NAME,                   /* no usable name in the command line */
    PROGMOD_ERR_DUPLICATE,              /* the name is taken */
    PROGMOD_ERR_FULL,                   /* PROGMOD_MAX programs already */
};

/* The program name from a module command line (cmdline may be NULL). */
enum progmod_result progmod_name(const char *cmdline, char out[PROGMOD_NAME_MAX]);

void progmod_init(struct progmod_table *t);
/* Adds a checked program; refuses a taken name, then a full table. */
enum progmod_result progmod_add(struct progmod_table *t, const char *name, uint32_t module,
                                uint64_t phys, uint64_t size, const struct elf_plan *plan);
const struct progmod *progmod_find(const struct progmod_table *t, const char *name);

/* A command line made safe to log: bytes outside 0x20-0x7E become '?', and
 * it is cut to PROGMOD_SHOW_MAX characters plus "..." (out: 44 bytes). */
#define PROGMOD_SHOW_BUF (PROGMOD_SHOW_MAX + 4u)
void progmod_show(const char *cmdline, char out[PROGMOD_SHOW_BUF]);

const char *progmod_result_str(enum progmod_result r);

#endif
