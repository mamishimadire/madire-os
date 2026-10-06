/* Unit tests for kernel/proc/userimg.c (built-in user program images, 0.10). */
#include <stdint.h>
#include <string.h>
#include "kernel/memory/vm_layout.h"
#include "kernel/proc/userimg.h"
#include "tests/unit/unit.h"

static const uint8_t code[64] = { 0x90 };
static const uint8_t data[16] = { 1 };

static struct userimg good(const char *name)
{
    struct userimg i = { name, code, sizeof(code), data, sizeof(data), 4096, VM_USER_TEXT };
    return i;
}

static void test_good_image(void)
{
    struct userimg i = good("hello");
    CHECK_EQ(userimg_check(&i), USERIMG_OK);
    i.data = 0;                                     /* no data at all */
    i.data_size = 0;
    i.data_mem = 0;
    CHECK_EQ(userimg_check(&i), USERIMG_OK);
    i = good("a_1");
    i.entry = VM_USER_TEXT + sizeof(code) - 1;      /* last byte of the code */
    CHECK_EQ(userimg_check(&i), USERIMG_OK);
}

static void test_names(void)
{
    struct userimg i = good("");
    CHECK_EQ(userimg_check(&i), USERIMG_ERR_NAME);
    i.name = 0;
    CHECK_EQ(userimg_check(&i), USERIMG_ERR_NAME);
    i.name = "Hello";                               /* lower case only */
    CHECK_EQ(userimg_check(&i), USERIMG_ERR_NAME);
    i.name = "bad name";
    CHECK_EQ(userimg_check(&i), USERIMG_ERR_NAME);
    i.name = "../hello";
    CHECK_EQ(userimg_check(&i), USERIMG_ERR_NAME);
    i.name = "exactly15chars_";
    CHECK_EQ(userimg_check(&i), USERIMG_OK);
    i.name = "sixteen_chars_xx";
    CHECK_EQ(userimg_check(&i), USERIMG_ERR_NAME);
}

static void test_sizes(void)
{
    struct userimg i = good("p");
    i.text_size = 0;
    CHECK_EQ(userimg_check(&i), USERIMG_ERR_TEXT);
    i = good("p");
    i.text = 0;
    CHECK_EQ(userimg_check(&i), USERIMG_ERR_TEXT);
    i = good("p");
    i.text_size = USERIMG_TEXT_MAX;
    CHECK_EQ(userimg_check(&i), USERIMG_OK);
    i.text_size = USERIMG_TEXT_MAX + 1;
    CHECK_EQ(userimg_check(&i), USERIMG_ERR_TEXT);
    i = good("p");
    i.data_mem = 8;                                 /* smaller than the data blob */
    CHECK_EQ(userimg_check(&i), USERIMG_ERR_DATA);
    i = good("p");
    i.data_mem = USERIMG_DATA_MAX;
    CHECK_EQ(userimg_check(&i), USERIMG_OK);
    i.data_mem = USERIMG_DATA_MAX + 1;
    CHECK_EQ(userimg_check(&i), USERIMG_ERR_DATA);
    i = good("p");
    i.data = 0;                                     /* data bytes but no blob */
    CHECK_EQ(userimg_check(&i), USERIMG_ERR_DATA);
    i = good("p");
    i.data_size = UINT64_MAX;
    i.data_mem = UINT64_MAX;
    CHECK_EQ(userimg_check(&i), USERIMG_ERR_DATA);
}

static void test_entry(void)
{
    struct userimg i = good("p");
    i.entry = VM_USER_TEXT - 1;
    CHECK_EQ(userimg_check(&i), USERIMG_ERR_ENTRY);
    i.entry = VM_USER_TEXT + sizeof(code);          /* just past the code */
    CHECK_EQ(userimg_check(&i), USERIMG_ERR_ENTRY);
    i.entry = VM_USER_DATA;
    CHECK_EQ(userimg_check(&i), USERIMG_ERR_ENTRY);
    i.entry = 0;
    CHECK_EQ(userimg_check(&i), USERIMG_ERR_ENTRY);
    i.entry = 0xFFFFFFFF80100000ull;
    CHECK_EQ(userimg_check(&i), USERIMG_ERR_ENTRY);
}

static void test_table_and_find(void)
{
    struct userimg t[3] = { good("exitcode"), good("hello"), good("spin") };
    uint64_t bad = 99;
    CHECK_EQ(userimg_check_table(t, 3, &bad), USERIMG_OK);
    CHECK_EQ(bad, 99);
    CHECK(userimg_find(t, 3, "hello") == &t[1]);
    CHECK(userimg_find(t, 3, "spin") == &t[2]);
    CHECK(userimg_find(t, 3, "hell") == 0);
    CHECK(userimg_find(t, 3, "hello2") == 0);
    CHECK(userimg_find(t, 3, "") == 0);
    CHECK(userimg_find(t, 3, 0) == 0);
    t[2].name = "hello";
    CHECK_EQ(userimg_check_table(t, 3, &bad), USERIMG_ERR_DUPLICATE);
    CHECK_EQ(bad, 2);
    t[2].name = "spin";
    t[1].entry = 0;
    CHECK_EQ(userimg_check_table(t, 3, &bad), USERIMG_ERR_ENTRY);
    CHECK_EQ(bad, 1);
    CHECK_EQ(userimg_check_table(t, 0, &bad), USERIMG_OK);
}

static void test_pages(void)
{
    struct userimg i = good("p");                   /* 64 bytes code, 4096 data + bss */
    CHECK_EQ(userimg_pages(&i), 1 + 1 + VM_USER_STACK_SIZE / 4096);
    i.text_size = 4097;
    i.data_mem = 0;
    CHECK_EQ(userimg_pages(&i), 2 + 0 + VM_USER_STACK_SIZE / 4096);
    CHECK(strcmp(userimg_result_str(USERIMG_ERR_ENTRY), "entry point outside the code") == 0);
}

int main(void)
{
    RUN(test_good_image);
    RUN(test_names);
    RUN(test_sizes);
    RUN(test_entry);
    RUN(test_table_and_find);
    RUN(test_pages);
    return unit_report("userimg");
}
