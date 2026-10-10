/* Program modules: registration at boot (see progmod.h). */
#include "kernel/kprintf.h"
#include "kernel/proc/progmod.h"

static struct progmod_table table;      /* zero: empty */
static uint32_t refused;
static struct progmod_candidate cand[PROGMOD_CANDIDATES_MAX];
static uint32_t cand_count;

static void note(uint32_t module, const char *name, const uint8_t *bytes, uint64_t size,
                 bool accepted, enum elf_result elf)
{
    if (cand_count >= PROGMOD_CANDIDATES_MAX)
        return;
    struct progmod_candidate *c = &cand[cand_count++];
    c->module = module;
    uint32_t i = 0;
    for (; name && name[i] && i < PROGMOD_NAME_MAX - 1; i++)
        c->name[i] = name[i];
    c->name[i] = '\0';
    c->bytes = bytes;
    c->size = size;
    c->accepted = accepted;
    c->elf = elf;
}

void progmod_register(uint32_t module, const char *cmdline, uint64_t phys,
                      const uint8_t *bytes, uint64_t size)
{
    char name[PROGMOD_NAME_MAX];
    enum progmod_result nr = progmod_name(cmdline, name);
    if (nr != PROGMOD_OK) {
        char show[PROGMOD_SHOW_BUF];
        progmod_show(cmdline, show);
        klogf("Program module %u: refused: %s ('%s')\n", module, progmod_result_str(nr), show);
        refused++;
        note(module, 0, bytes, size, false, ELF_OK);
        return;
    }
    struct elf_plan plan;
    enum elf_result er = elf_check(bytes, size, &plan);
    if (er != ELF_OK) {
        klogf("Program module %u '%s': %llu bytes, refused: %s\n", module, name,
              (unsigned long long)size, elf_result_str(er));
        refused++;
        note(module, name, bytes, size, false, er);
        return;
    }
    enum progmod_result ar = progmod_add(&table, name, module, phys, size, &plan);
    if (ar != PROGMOD_OK) {
        klogf("Program module %u '%s': %llu bytes, refused: %s\n", module, name,
              (unsigned long long)size, progmod_result_str(ar));
        refused++;
        note(module, name, bytes, size, false, ELF_OK);
        return;
    }
    note(module, name, bytes, size, true, ELF_OK);
    klogf("Program module %u '%s': %llu bytes, ELF checked: %u segments, %u pages, entry 0x%llx\n",
          module, name, (unsigned long long)size, plan.count, plan.pages,
          (unsigned long long)plan.entry);
}

void progmod_summary(void)
{
    if (table.count + refused == 0)
        return;
    klogf("Program modules: %u accepted, %u refused (at most %u)\n", table.count, refused,
          PROGMOD_MAX);
}

const struct progmod_table *progmod_table(void)
{
    return &table;
}

const struct progmod_candidate *progmod_candidates(uint32_t *count)
{
    *count = cand_count;
    return cand;
}
