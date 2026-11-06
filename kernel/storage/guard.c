/* The write guard at attach (1.4 stage 5): see guard.h. Read only. */
#include "kernel/storage/guard.h"
#include "kernel/kprintf.h"
#include "kernel/storage/ahci.h"
#include "kernel/storage/blk.h"
#include "kernel/storage/guard_core.h"
#include "kernel/storage/part.h"

typedef unsigned long long ull;

static uint8_t guard_marker_sec[GUARD_SECTOR];
static uint8_t guard_sb_sec[GUARD_SECTOR];
static struct guard_verdict guard_v;
static char guard_why[160];

/* The failed condition in full, for a guard: line. */
static void guard_reason(char *buf, size_t size, const struct guard_verdict *v)
{
    switch (v->cond) {
    case GUARD_OK:
        ksnprintf(buf, size, "all three conditions hold");
        return;
    case GUARD_A_MODEL:
        ksnprintf(buf, size, "(a) model '%s' is not QEMU HARDDISK or VBOX HARDDISK", v->model);
        return;
    case GUARD_B_SMALL:
        ksnprintf(buf, size, "(b) the partition's %llu sectors cannot hold the test marker and the ext2 superblock "
                  "(%llu needed)", (ull)v->got, (ull)v->want);
        return;
    case GUARD_B_MAGIC:
        ksnprintf(buf, size, "(b) no Madire test marker (MADIRE-TEST-DISK) in the partition's first sector");
        return;
    case GUARD_B_VERSION:
        ksnprintf(buf, size, "(b) test marker version %llu, not %llu", (ull)v->got, (ull)v->want);
        return;
    case GUARD_B_LENGTH:
        ksnprintf(buf, size, "(b) test marker record length %llu, not %llu", (ull)v->got, (ull)v->want);
        return;
    case GUARD_B_CRC:
        ksnprintf(buf, size, "(b) test marker CRC32 0x%08llx does not match its record (computed 0x%08llx)",
                  (ull)v->got, (ull)v->want);
        return;
    case GUARD_B_START:
        ksnprintf(buf, size, "(b) test marker start LBA %llu, the partition starts at LBA %llu", (ull)v->got,
                  (ull)v->want);
        return;
    case GUARD_B_SECTORS:
        ksnprintf(buf, size, "(b) test marker length %llu sectors, the partition has %llu", (ull)v->got,
                  (ull)v->want);
        return;
    case GUARD_B_TAIL:
        ksnprintf(buf, size, "(b) test marker sector not zero after the record (byte %llu)", (ull)v->got);
        return;
    case GUARD_C_MAGIC:
        ksnprintf(buf, size, "(c) no ext2 superblock at partition byte 1024 (magic 0x%04llx, not 0x%04llx)",
                  (ull)v->got, (ull)v->want);
        return;
    case GUARD_C_LABEL:
        ksnprintf(buf, size, "(c) ext2 label '%s', not 'madire-test'", v->label);
        return;
    }
    ksnprintf(buf, size, "unknown");
}

/* The condition in short, for the Storage: line. */
static const char *guard_short(enum guard_cond cond)
{
    const char *l = guard_cond_letter(cond);
    if (l[0] && l[1] == 'a')
        return "(a) not a virtual disk";
    if (l[0] && l[1] == 'b')
        return "(b) no valid test marker";
    if (l[0] && l[1] == 'c')
        return "(c) not labelled madire-test";
    return "unknown";
}

/* Conditions (b) and (c) (and (a) again) for one partition; logs its line. */
static bool guard_partition(uint32_t port, const char *model, const struct part_entry *e, const char **why_short)
{
    const uint8_t *marker = 0, *sb = 0;
    if (e->sectors >= GUARD_MIN_SECTORS) {
        if (blk_read(e->first, 1, guard_marker_sec) != 0 ||
            blk_read(e->first + GUARD_SB_SECTOR, 1, guard_sb_sec) != 0) {
            klogf("guard: port %u partition %u: writes would be REFUSED: the partition's first sectors could not "
                  "be read (EIO)\n", port, e->index);
            *why_short = "a sector could not be read";
            return false;
        }
        marker = guard_marker_sec;
        sb = guard_sb_sec;
    }
    if (guard_check(&guard_v, model, e->first, e->sectors, marker, sb)) {
        klogf("guard: port %u partition %u: writes would be ALLOWED (virtual disk '%s'; test marker for LBA %llu, "
              "%llu sectors; ext2 label '%s')\n", port, e->index, guard_v.model, (ull)e->first, (ull)e->sectors,
              guard_v.label);
        return true;
    }
    guard_reason(guard_why, sizeof(guard_why), &guard_v);
    klogf("guard: port %u partition %u: writes would be REFUSED: %s\n", port, e->index, guard_why);
    *why_short = guard_short(guard_v.cond);
    return false;
}

void guard_attach(uint32_t port, const char *model, uint64_t sectors, struct guard_summary *s)
{
    const struct part_table *t = part_attach(port, sectors);
    const struct part_entry *chosen = 0;
    const char *why_short = 0;
    uint32_t candidates = 0;

    if (t->kind == PART_KIND_MBR || t->kind == PART_KIND_GPT)
        ksnprintf(s->table, sizeof(s->table), "%s, %u partition%s", part_kind_str(t->kind), t->n,
                  t->n == 1 ? "" : "s");
    else
        ksnprintf(s->table, sizeof(s->table), "%s", part_kind_str(t->kind));

    if (!guard_model_virtual(model, guard_v.model)) {
        klogf("guard: port %u: writes would be REFUSED: (a) model '%s' is not QEMU HARDDISK or VBOX HARDDISK\n",
              port, guard_v.model);
        why_short = guard_short(GUARD_A_MODEL);
    } else if (t->kind == PART_KIND_NONE || t->kind == PART_KIND_BAD) {
        why_short = t->kind == PART_KIND_NONE ? "no partition table" : "the partition table is not usable";
        klogf("guard: port %u: writes would be REFUSED: %s\n", port, why_short);
    } else {
        for (uint32_t i = 0; i < t->n && !chosen; i++) {
            const struct part_entry *e = &t->e[i];
            const char *w = 0;
            if (!e->linux_fs)
                continue;
            candidates++;
            if (guard_partition(port, model, e, &w))
                chosen = e;
            else if (!why_short)
                why_short = w;
        }
        if (!candidates) {
            why_short = "no Linux partition";
            klogf("guard: port %u: writes would be REFUSED: no Linux partition (MBR type 0x83 or GPT Linux "
                  "filesystem data)\n", port);
        }
    }

    ahci_disk_guard(port, chosen != 0, chosen ? chosen->first : 0, chosen ? chosen->sectors : 0);
    s->allows = ahci_disk_guard_allows(port);
    s->partition = s->allows && chosen ? chosen->index : 0;
    if (s->allows) {
        ksnprintf(s->result, sizeof(s->result), "ALLOWED on partition %u", s->partition);
        klogf("guard: port %u: decided once, kept by the AHCI driver: writes would be ALLOWED on partition %u (LBA "
              "%llu, %llu sectors); no write command exists in stage 5\n", port, s->partition,
              (ull)chosen->first, (ull)chosen->sectors);
    } else {
        ksnprintf(s->result, sizeof(s->result), "REFUSED: %s",
                  why_short ? why_short : "the decision was not recorded");
        klogf("guard: port %u: decided once, kept by the AHCI driver: writes would be REFUSED; no write command "
              "exists in stage 5\n", port);
    }
}
