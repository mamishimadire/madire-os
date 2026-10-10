/* Unit tests for the 0.11 load plans (kernel/proc/loadplan.c): built-in
 * images and ELF plans as segments, and which bytes go where in each page. */
#include <stdint.h>
#include <string.h>
#include "kernel/memory/vm_layout.h"
#include "kernel/proc/loadplan.h"
#include "tests/unit/unit.h"

static uint8_t text[5000], data[300];

static struct userimg image(uint64_t text_size, uint64_t data_size, uint64_t data_mem)
{
    struct userimg img = { "prog", text, text_size, data, data_size, data_mem, VM_USER_TEXT };
    return img;
}

static void test_from_userimg(void)
{
    struct load_plan p;
    struct userimg img = image(5000, 300, 0x1388);
    CHECK(loadplan_from_userimg(&img, &p));
    CHECK_EQ(p.entry, VM_USER_TEXT);
    CHECK_EQ(p.count, 2);
    CHECK_EQ(p.seg[0].vaddr, VM_USER_TEXT);
    CHECK_EQ(p.seg[0].filesz, 5000);
    CHECK_EQ(p.seg[0].memsz, 5000);
    CHECK(p.seg[0].src == text && p.seg[0].exec && !p.seg[0].write);
    CHECK_EQ(loadplan_pages(&p.seg[0]), 2);                  /* as before: vm_ceil(5000) */
    CHECK_EQ(p.seg[1].vaddr, VM_USER_DATA);
    CHECK_EQ(p.seg[1].filesz, 300);
    CHECK_EQ(p.seg[1].memsz, 0x1388);
    CHECK(p.seg[1].src == data && p.seg[1].write && !p.seg[1].exec);
    CHECK_EQ(loadplan_pages(&p.seg[1]), 2);                  /* vm_ceil(data_mem) */

    img = image(100, 0, 0);                                  /* no data at all */
    CHECK(loadplan_from_userimg(&img, &p));
    CHECK_EQ(p.count, 1);
    CHECK_EQ(loadplan_pages(&p.seg[0]), 1);
    img = image(100, 0, 0x2000);                             /* bss only */
    CHECK(loadplan_from_userimg(&img, &p));
    CHECK_EQ(p.count, 2);
    CHECK_EQ(p.seg[1].filesz, 0);
    CHECK_EQ(loadplan_pages(&p.seg[1]), 2);
    img = image(0, 0, 0);                                    /* no code: refused */
    CHECK(!loadplan_from_userimg(&img, &p));
    img = image(100, 0, 0);
    img.entry = VM_USER_TEXT + 100;                          /* entry past the code */
    CHECK(!loadplan_from_userimg(&img, &p));
}

static void test_from_elf(void)
{
    static uint8_t file[0x3000];
    struct elf_plan e;
    memset(&e, 0, sizeof(e));
    e.entry = 0x400010;
    e.count = 3;
    e.seg[0] = (struct elf_segment){ 0x400010, 0x1010, 0x100, 0x100, false, true };
    e.seg[1] = (struct elf_segment){ 0x401000, 0x2000, 0x40, 0x40, false, false };
    e.seg[2] = (struct elf_segment){ 0x500000, 0, 0, 0x1388, true, false };
    struct load_plan p;
    loadplan_from_elf(file, &e, &p);
    CHECK_EQ(p.entry, 0x400010);
    CHECK_EQ(p.count, 3);
    CHECK(p.seg[0].src == file + 0x1010 && p.seg[0].exec && !p.seg[0].write);
    CHECK(p.seg[1].src == file + 0x2000 && !p.seg[1].exec && !p.seg[1].write);   /* read-only */
    CHECK(p.seg[2].src == file && p.seg[2].write && p.seg[2].filesz == 0);
    CHECK_EQ(p.seg[2].memsz, 0x1388);
}

static void test_pages(void)
{
    uint64_t src_off;
    uint32_t at, len;
    /* Page-aligned, one full page and a part. */
    struct load_segment s = { 0x400000, 5000, 5000, text, false, true };
    CHECK_EQ(loadplan_first_page(&s), 0x400000);
    CHECK_EQ(loadplan_pages(&s), 2);
    loadplan_page(&s, 0x400000, &src_off, &at, &len);
    CHECK(src_off == 0 && at == 0 && len == 4096);
    loadplan_page(&s, 0x401000, &src_off, &at, &len);
    CHECK(src_off == 4096 && at == 0 && len == 5000 - 4096);
    /* Starting inside a page: the bytes go at that offset. */
    struct load_segment u = { 0x400010, 0x100, 0x100, text, false, true };
    CHECK_EQ(loadplan_first_page(&u), 0x400000);
    CHECK_EQ(loadplan_pages(&u), 1);
    loadplan_page(&u, 0x400000, &src_off, &at, &len);
    CHECK(src_off == 0 && at == 0x10 && len == 0x100);
    /* Starting inside a page and crossing into the next. */
    struct load_segment c = { 0x400F00, 0x300, 0x300, text, false, true };
    CHECK_EQ(loadplan_pages(&c), 2);
    loadplan_page(&c, 0x400000, &src_off, &at, &len);
    CHECK(src_off == 0 && at == 0xF00 && len == 0x100);
    loadplan_page(&c, 0x401000, &src_off, &at, &len);
    CHECK(src_off == 0x100 && at == 0 && len == 0x200);
    /* File bytes then bss: the bss-only pages get no bytes. */
    struct load_segment d = { 0x500000, 0x10, 0x3000, data, true, false };
    CHECK_EQ(loadplan_pages(&d), 3);
    loadplan_page(&d, 0x500000, &src_off, &at, &len);
    CHECK(src_off == 0 && at == 0 && len == 0x10);
    loadplan_page(&d, 0x501000, &src_off, &at, &len);
    CHECK(src_off == 0 && at == 0 && len == 0);
    loadplan_page(&d, 0x502000, &src_off, &at, &len);
    CHECK(len == 0 && at == 0);
    /* No file bytes at all. */
    struct load_segment z = { 0x600000, 0, 0x1388, 0, true, false };
    CHECK_EQ(loadplan_pages(&z), 2);
    loadplan_page(&z, 0x600000, &src_off, &at, &len);
    CHECK(len == 0 && at == 0 && src_off == 0);
    /* Every page of a long, unaligned segment: the pieces add up to filesz,
     * in order, with no gap and no overlap. */
    struct load_segment l = { 0x400123, 0x4567, 0x9000, text, false, true };
    uint64_t next = 0, total = 0;
    uint64_t page = loadplan_first_page(&l);
    bool in_order = true;
    for (uint32_t i = 0; i < loadplan_pages(&l); i++, page += 0x1000) {
        loadplan_page(&l, page, &src_off, &at, &len);
        if (len) {
            in_order = in_order && src_off == next && page + at == l.vaddr + src_off &&
                       at + len <= 0x1000;
            next += len;
            total += len;
        }
    }
    CHECK(in_order);
    CHECK_EQ(total, 0x4567);
    CHECK_EQ(loadplan_pages(&l), (0x400123 + 0x9000 + 0xFFF) / 0x1000 - 0x400);
}

int main(void)
{
    RUN(test_from_userimg);
    RUN(test_from_elf);
    RUN(test_pages);
    return unit_report("loadplan");
}
