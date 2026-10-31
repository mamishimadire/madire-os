/* The block cache in the kernel and test=diskread (1.4 stage 4): see
 * blk.h. Read only. */
#include "kernel/storage/blk.h"
#include "kernel/kprintf.h"
#include "kernel/memory/kmalloc.h"
#include "kernel/memory/memory.h"
#include "kernel/memory/paging.h"
#include "kernel/memory/pmm.h"
#include "kernel/memory/pte.h"
#include "kernel/memory/vm_layout.h"
#include "kernel/proc/diagwait.h"
#include "kernel/proc/fd.h"
#include "kernel/proc/preempt.h"
#include "kernel/proc/thread.h"
#include "kernel/storage/ahci.h"
#include "kernel/storage/blk_core.h"
#include "kernel/timer.h"

typedef unsigned long long ull;

static struct blk_cache blk_cache;
static struct blk_slot blk_slots[VM_BLK_CACHE_PAGES];
static uint32_t blk_frames[VM_BLK_CACHE_PAGES];     /* each slot's frame, 0 until made */
static uint32_t blk_port;
static bool blk_on;
static uint8_t blk_sector[BLK_SECTOR];

/* Slot `slot`'s page: a frame of free RAM at VM_BLK_CACHE_BASE + slot pages. */
static bool blk_ready(void *ctx, uint32_t slot)
{
    (void)ctx;
    if (blk_frames[slot])
        return true;
    uint32_t frame = memory_alloc_page();
    if (!frame)
        return false;
    if (paging_map(VM_BLK_CACHE_BASE + (uint64_t)slot * VM_PAGE_SIZE, frame, PTE_WRITE | paging_nx_flag()) !=
        VMM_OK) {
        memory_free_page(frame);
        return false;
    }
    blk_frames[slot] = frame;
    return true;
}

static uint8_t *blk_data(void *ctx, uint32_t slot)
{
    (void)ctx;
    return (uint8_t *)(uintptr_t)(VM_BLK_CACHE_BASE + (uint64_t)slot * VM_PAGE_SIZE);
}

static int blk_disk(void *ctx, uint64_t lba, uint32_t sectors, const uint32_t *slots, uint32_t n)
{
    (void)ctx;
    uint32_t phys[BLK_RUN_MAX];
    for (uint32_t i = 0; i < n && i < BLK_RUN_MAX; i++)
        phys[i] = blk_frames[slots[i]];
    return ahci_disk_read(blk_port, lba, sectors, phys, n) ? -BLK_EIO : 0;
}

static const struct blk_ops blk_kernel_ops = { blk_ready, blk_data, blk_disk, 0 };

bool blk_attached(void)
{
    return blk_on;
}

uint32_t blk_pages_made(void)
{
    return blk_on ? blk_cache.made : 0;
}

int blk_read(uint64_t lba, uint32_t count, uint8_t *dst)
{
    if (!blk_on)
        return -BLK_EIO;
    return blk_cache_read(&blk_cache, lba, count, dst);
}

/* One sector's CRC32, or false on an error. */
static bool blk_sector_crc(uint64_t lba, uint32_t *crc)
{
    if (blk_read(lba, 1, blk_sector) != 0)
        return false;
    *crc = blk_crc32(0, blk_sector, BLK_SECTOR);
    return true;
}

void blk_attach(uint32_t port, uint64_t sectors)
{
    uint32_t free_pages = pmm_free_pages(), cap = blk_cache_pages(free_pages);
    blk_port = port;
    blk_cache_init(&blk_cache, &blk_kernel_ops, blk_slots, cap, sectors);
    blk_on = true;
    klogf("blk: disk on port %u, %llu sectors; cache up to %u pages (%u KiB: 2 MiB or 1/16 of the %u free pages, "
          "the smaller), each made on first use; read only\n", port, (ull)sectors, cap, cap * 4u, free_pages);
    uint32_t c0 = 0, c1 = 0;
    uint64_t t0 = timer_ticks();
    bool ok0 = blk_sector_crc(0, &c0), ok1 = blk_sector_crc(sectors - 1, &c1);
    uint64_t t1 = timer_ticks();
    if (ok0 && ok1)
        klogf("blk: read check: LBA 0 CRC32 0x%08x, LBA %llu CRC32 0x%08x (%llu commands, %llu ticks)\n", c0,
              (ull)(sectors - 1), c1, (ull)blk_cache.commands, (ull)(t1 - t0));
    else
        klogf("blk: read check: LBA 0 %s, LBA %llu %s (EIO: see the ahci: line)\n", ok0 ? "read" : "not read",
              (ull)(sectors - 1), ok1 ? "read" : "not read");
}

/* ---- test=diskread ---- */

#define BLK_DR_READS    13u
#define BLK_DR_CHUNK    128u            /* sectors per blk_read: 64 KiB, 16 pages */
#define BLK_DR_BIG      8192u           /* 4 MiB: twice the cache at most */
#define BLK_DR_MIN      16384u          /* the smallest disk the test reads */

struct blk_dr_read {
    uint64_t lba;
    uint32_t count;
    bool beyond;                        /* past the end: must end with EIO */
    const char *what;
    /* results */
    int result;
    uint32_t crc;
    uint64_t cycles, commands, hits, misses;
};

struct blk_dr {
    struct blk_dr_read r[BLK_DR_READS];
    uint8_t *buf;
    uint64_t longest_cycles;            /* the longest blk_read with preemption off */
    volatile bool done;
};

static struct blk_dr blk_dr;

static int32_t blk_dr_reader(void *arg)
{
    struct blk_dr *d = arg;
    for (uint32_t i = 0; i < BLK_DR_READS; i++) {
        struct blk_dr_read *r = &d->r[i];
        uint64_t c0 = fd_tsc(), h0 = blk_cache.hits, m0 = blk_cache.misses, k0 = blk_cache.commands;
        uint32_t crc = 0;
        int res = 0;
        if (r->beyond) {
            preempt_disable();
            res = blk_read(r->lba, r->count, d->buf);
            preempt_enable();
        }
        for (uint32_t done = 0; !r->beyond && done < r->count && res == 0;) {
            uint32_t n = r->count - done < BLK_DR_CHUNK ? r->count - done : BLK_DR_CHUNK;
            uint64_t s0 = fd_tsc();
            preempt_disable();                  /* as a holder of the VFS lock reads (risk 26) */
            res = blk_read(r->lba + done, n, d->buf);
            preempt_enable();
            uint64_t s1 = fd_tsc() - s0;
            if (s1 > d->longest_cycles)
                d->longest_cycles = s1;
            if (res == 0)
                crc = blk_crc32(crc, d->buf, n * BLK_SECTOR);
            done += n;
        }
        r->result = res;
        r->crc = crc;
        r->cycles = fd_tsc() - c0;
        r->hits = blk_cache.hits - h0;
        r->misses = blk_cache.misses - m0;
        r->commands = blk_cache.commands - k0;
    }
    d->done = true;
    return 0;
}

static void blk_dr_set(struct blk_dr_read *r, uint64_t lba, uint32_t count, bool beyond, const char *what)
{
    r->lba = lba;
    r->count = count;
    r->beyond = beyond;
    r->what = what;
    r->result = 0;
    r->crc = 0;
    r->cycles = r->commands = r->hits = r->misses = 0;
}

void blk_diskread(void)
{
    if (!blk_on || blk_cache.sectors < BLK_DR_MIN) {
        klogf("diskread: FAILED (%s)\n", blk_on ? "the disk is smaller than 16384 sectors" : "no disk attached");
        return;
    }
    uint64_t s = blk_cache.sectors;
    struct blk_dr *d = &blk_dr;
    blk_dr_set(&d->r[0], 0, 1, false, "the first sector");
    blk_dr_set(&d->r[1], s / 2, 1, false, "the middle sector");
    blk_dr_set(&d->r[2], s - 1, 1, false, "the last sector");
    blk_dr_set(&d->r[3], 8, 128, false, "16 whole pages, one command");
    blk_dr_set(&d->r[4], 1001, 300, false, "an unaligned run across pages");
    blk_dr_set(&d->r[5], 4096, BLK_DR_BIG, false, "4 MiB, more than the cache holds");
    blk_dr_set(&d->r[6], 0, 1, false, "the first sector again, after the cache was replaced");
    blk_dr_set(&d->r[7], s - 11, 11, false, "the last 11 sectors");
    blk_dr_set(&d->r[8], s - 3, 3, false, "the last 3 sectors");
    blk_dr_set(&d->r[9], 8, 128, false, "the 16 pages again");
    blk_dr_set(&d->r[10], s, 1, true, "the sector after the last");
    blk_dr_set(&d->r[11], s - 2, 4, true, "a run across the end");
    blk_dr_set(&d->r[12], 0xFFFFFFFFFFFFull, 1, true, "a sector far beyond the end");
    d->buf = kmalloc(BLK_DR_CHUNK * BLK_SECTOR);
    if (!d->buf) {
        klogf("diskread: FAILED (no heap for the 64 KiB buffer)\n");
        return;
    }
    uint64_t spent = 0, per_tick = fd_tsc_per_tick(&spent);
    d->longest_cycles = 0;
    d->done = false;
    klogf("diskread: %u reads on port %u (%llu sectors); each blk_read of at most %u sectors with preemption "
          "off; TSC %llu cycles per tick\n", BLK_DR_READS, blk_port, (ull)s, BLK_DR_CHUNK, (ull)per_tick);
    thread_turn_reset();
    int32_t tid = thread_create("diskread", blk_dr_reader, d, false);
    if (tid < 0) {
        kfree(d->buf);
        klogf("diskread: FAILED (no thread)\n");
        return;
    }
    for (uint32_t i = 0; i < 120u * TIMER_HZ && !d->done; i++)
        diag_sleep(1);                  /* thread 0 stays ready: the reader's turns end at switches */
    thread_join(tid);
    char owner[96];
    uint64_t turn = thread_turn_max();
    thread_turn_owner(owner, sizeof(owner));

    uint32_t bad = 0, eio = 0;
    for (uint32_t i = 0; i < BLK_DR_READS; i++) {
        const struct blk_dr_read *r = &d->r[i];
        bool ok = r->beyond ? r->result == -BLK_EIO : r->result == 0;
        bad += !ok;
        eio += r->beyond && r->result == -BLK_EIO;
        if (r->result == 0)
            klogf("diskread: read %u: LBA %llu, %u sectors: CRC32 0x%08x (%s; %llu us, %llu commands, %llu pages "
                  "from the cache, %llu read)%s\n", i + 1, (ull)r->lba, r->count, r->crc, r->what,
                  (ull)fd_cycles_us(r->cycles, per_tick), (ull)r->commands, (ull)r->hits, (ull)r->misses,
                  ok ? "" : " NOT EXPECTED");
        else
            klogf("diskread: read %u: LBA %llu, %u sectors: EIO (%s)%s\n", i + 1, (ull)r->lba, r->count, r->what,
                  ok ? ", as expected" : " NOT EXPECTED");
    }
    uint64_t commands, polls, ticks;
    ahci_figures(&commands, &polls, &ticks);
    const struct blk_dr_read *big = &d->r[5];
    uint64_t big_us = fd_cycles_us(big->cycles, per_tick);
    klogf("diskread: speed (report only): %u KiB in %llu us, %llu KiB/s\n", BLK_DR_BIG / 2u, (ull)big_us,
          (ull)(big_us ? (uint64_t)(BLK_DR_BIG / 2u) * 1000000u / big_us : 0));
    klogf("diskread: longest turn (report only): %llu ticks (%s); the longest blk_read with preemption off "
          "%llu us; the longest command %llu ticks, %llu register reads; %llu commands sent\n", (ull)turn, owner,
          (ull)fd_cycles_us(d->longest_cycles, per_tick), (ull)ticks, (ull)polls, (ull)commands);
    klogf("diskread: cache: %u of %u pages made, %llu pages from the cache, %llu read, %llu commands, %llu "
          "replaced, %llu reads refused (beyond the end), %llu errors\n", blk_cache.made, blk_cache.cap,
          (ull)blk_cache.hits, (ull)blk_cache.misses, (ull)blk_cache.commands, (ull)blk_cache.replaced,
          (ull)blk_cache.refused, (ull)blk_cache.errors);
    uint32_t tables, mapped;
    paging_slot_figures(VM_STORAGE_INDEX, &tables, &mapped);
    klogf("diskread: slot 508 now: %u page tables, %u pages mapped (walked)\n", tables, mapped);
    kfree(d->buf);
    if (bad || !d->done)
        klogf("diskread: FAILED (%u reads ended otherwise than expected%s)\n", bad,
              d->done ? "" : "; the reader did not finish");
    else
        klogf("diskread: DONE (%u reads; %u beyond the end ended with EIO, as expected)\n", BLK_DR_READS, eio);
}
