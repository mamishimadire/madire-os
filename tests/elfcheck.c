/* Host tool (0.11): runs the kernel's own ELF checks (kernel/proc/elf_core.c)
 * on each file given, and prints one line per file:
 *   <name>: OK, <n> segments, <p> pages, entry 0x<entry>
 *   <name>: refused: <reason>
 * tests/qemu-tests.sh runs it on the built programs (build/user/<name>.elf) and
 * on the malformed fixtures from tests/elf-fixtures.py. Exit status 0 unless
 * a file cannot be read. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/proc/elf_core.h"

static const char *base_name(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

int main(int argc, char **argv)
{
    /* One byte more than the cap, so a file over it is seen as such. */
    static uint8_t buf[ELF_FILE_MAX + 1];
    int status = 0;
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        if (!f) {
            printf("%s: cannot be read\n", base_name(argv[i]));
            status = 1;
            continue;
        }
        size_t n = fread(buf, 1, sizeof(buf), f);
        fclose(f);
        struct elf_plan plan;
        enum elf_result r = elf_check(buf, n, &plan);
        if (r == ELF_OK)
            printf("%s: OK, %u segments, %u pages, entry 0x%llx\n", base_name(argv[i]),
                   plan.count, plan.pages, (unsigned long long)plan.entry);
        else
            printf("%s: refused: %s\n", base_name(argv[i]), elf_result_str(r));
    }
    return status;
}
