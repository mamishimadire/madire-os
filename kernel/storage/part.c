/* Partition tables in the kernel (1.4 stage 5): see part.h. Read only. */
#include "kernel/storage/part.h"
#include "kernel/kprintf.h"
#include "kernel/storage/blk.h"

typedef unsigned long long ull;

static struct part_table part_tab;

static int part_sector(void *ctx, uint64_t lba, uint8_t *sector)
{
    (void)ctx;
    return blk_read(lba, 1, sector);
}

static const struct part_ops part_kernel_ops = { part_sector, 0 };

/* A GPT type GUID as text (the usual form, from its mixed-endian bytes). */
static void part_guid_text(char *buf, size_t size, const uint8_t *g)
{
    unsigned a = (unsigned)g[0] | (unsigned)g[1] << 8 | (unsigned)g[2] << 16 | (unsigned)g[3] << 24;
    unsigned b = (unsigned)g[4] | (unsigned)g[5] << 8, c = (unsigned)g[6] | (unsigned)g[7] << 8;
    ksnprintf(buf, size, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", a, b, c, (unsigned)g[8],
              (unsigned)g[9], (unsigned)g[10], (unsigned)g[11], (unsigned)g[12], (unsigned)g[13], (unsigned)g[14],
              (unsigned)g[15]);
}

const struct part_table *part_attach(uint32_t port, uint64_t sectors)
{
    struct part_table *t = &part_tab;
    char guid[40];
    guid[0] = 0;
    part_read(t, &part_kernel_ops, sectors);
    switch (t->kind) {
    case PART_KIND_NONE:
        klogf("part: port %u: no partition table (LBA 0 has no 0x55AA signature)\n", port);
        break;
    case PART_KIND_BAD:
        klogf("part: port %u: partition table not usable: %s; no partitions\n", port, t->why ? t->why : "unknown");
        break;
    case PART_KIND_MBR:
        klogf("part: port %u: MBR (disk signature 0x%08x), %u partition%s\n", port, t->mbr_signature, t->n,
              t->n == 1 ? "" : "s");
        break;
    case PART_KIND_GPT:
        klogf("part: port %u: GPT (protective MBR 0xEE): revision 1.0, header %u bytes, %u entries of %u bytes at "
              "LBA %llu, usable LBA %llu-%llu, header and entry-array CRC32 valid (0x%08x, 0x%08x), %u partition%s\n",
              port, t->gpt_header_size, t->gpt_entries, t->gpt_entry_size, (ull)t->gpt_entries_lba,
              (ull)t->gpt_first_usable, (ull)t->gpt_last_usable, t->gpt_header_crc, t->gpt_array_crc, t->n,
              t->n == 1 ? "" : "s");
        break;
    }
    for (uint32_t i = 0; i < t->n; i++) {
        const struct part_entry *e = &t->e[i];
        if (t->kind == PART_KIND_MBR) {
            klogf("part: port %u: partition %u: MBR type 0x%02x%s, LBA %llu, %llu sectors\n", port, e->index,
                  (unsigned)e->mbr_type, e->linux_fs ? " (Linux)" : "", (ull)e->first, (ull)e->sectors);
        } else if (e->linux_fs) {
            klogf("part: port %u: partition %u: GPT Linux filesystem data, LBA %llu, %llu sectors, name '%s'\n", port,
                  e->index, (ull)e->first, (ull)e->sectors, e->name);
        } else {
            part_guid_text(guid, sizeof(guid), e->type_guid);
            klogf("part: port %u: partition %u: GPT type %s, LBA %llu, %llu sectors, name '%s'\n", port, e->index,
                  guid, (ull)e->first, (ull)e->sectors, e->name);
        }
    }
    if (t->kind == PART_KIND_GPT) {
        if (t->backup_ok)
            klogf("part: port %u: GPT backup header at LBA %llu: valid (report only)\n", port,
                  (ull)t->gpt_alternate);
        else
            klogf("part: port %u: GPT backup header: NOT valid: %s (report only)\n", port,
                  t->backup_why ? t->backup_why : "unknown");
    }
    return t;
}
