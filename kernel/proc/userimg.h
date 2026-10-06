#ifndef MADIRE_PROC_USERIMG_H
#define MADIRE_PROC_USERIMG_H

/*
 * Built-in user program images (0.10). The programs in user/ are linked at
 * fixed addresses (user/link.ld) and turned into two raw blobs each: code +
 * read-only data, loaded at VM_USER_TEXT (read + execute), and data, loaded
 * at VM_USER_DATA (read + write, no-execute) and followed by data_mem -
 * data_size zero bytes (bss). The build generates the table of images
 * (scripts/user-images.sh); 0.11 brings ELF programs from boot modules.
 *
 * The images are built with the kernel, but the kernel still checks every
 * entry before using it (userimg_check), as it will check ELF files later.
 * Pure module: tests/unit/test_userimg.c.
 */

#include <stdbool.h>
#include <stdint.h>

#define USERIMG_TEXT_MAX (64u * 1024)   /* code + read-only data */
#define USERIMG_DATA_MAX (64u * 1024)   /* data + bss */
#define USERIMG_NAME_MAX 15u            /* a process name: PROC_NAME_MAX - 1 */

/* Seven 64-bit fields: the layout the generated assembly table uses. */
struct userimg {
    const char *name;
    const uint8_t *text;
    uint64_t text_size;             /* bytes of the text blob */
    const uint8_t *data;
    uint64_t data_size;             /* bytes of the data blob */
    uint64_t data_mem;              /* data + bss bytes from VM_USER_DATA */
    uint64_t entry;                 /* _start */
};

enum userimg_result {
    USERIMG_OK,
    USERIMG_ERR_NAME,               /* empty, too long, or not [a-z0-9_] */
    USERIMG_ERR_TEXT,               /* no code, too large, or no text pointer */
    USERIMG_ERR_DATA,               /* data_size > data_mem, too large, or no data pointer */
    USERIMG_ERR_ENTRY,              /* entry outside the code */
    USERIMG_ERR_DUPLICATE,          /* two images with the same name */
};

enum userimg_result userimg_check(const struct userimg *img);

/* Checks every image and that names are unique; *bad = the index of the
 * first failing image (or of the second of two equal names). */
enum userimg_result userimg_check_table(const struct userimg *imgs, uint64_t count, uint64_t *bad);

const struct userimg *userimg_find(const struct userimg *imgs, uint64_t count, const char *name);

/* Pages a process of this image uses for code, data + bss and the stack. */
uint32_t userimg_pages(const struct userimg *img);

const char *userimg_result_str(enum userimg_result result);

#endif
