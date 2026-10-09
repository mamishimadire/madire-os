/* Unit tests for kernel/proc/faultop.h (instruction bytes at a fault, 0.10). */
#include <stdint.h>
#include <string.h>
#include "kernel/proc/faultop.h"
#include "tests/unit/unit.h"

static struct faultop op(unsigned n, uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    struct faultop o = { (uint8_t)n, { a, b, c, d } };
    return o;
}

static void test_match(void)
{
    struct faultop want = op(2, 0x0f, 0x0b, 0, 0);                  /* ud2 */
    CHECK(faultop_match(&want, &want));
    struct faultop got = op(4, 0x0f, 0x0b, 0x48, 0x89);             /* ud2, then more code */
    CHECK(faultop_match(&got, &want));
    struct faultop cli = op(1, 0xfa, 0, 0, 0);
    struct faultop got_cli = op(4, 0xfa, 0x0f, 0x0b, 0x00);
    CHECK(faultop_match(&got_cli, &cli));
}

/* The case that slipped through in stage 4d: the expected MMX instruction
 * did not fault, and a later syscall (0f 05) did. */
static void test_rejects_wrong_opcode(void)
{
    struct faultop want = op(3, 0x0f, 0xef, 0xc0, 0);               /* pxor %mm0, %mm0 */
    struct faultop syscall = op(4, 0x0f, 0x05, 0x48, 0x83);
    CHECK(!faultop_match(&syscall, &want));
    struct faultop emms = op(4, 0x0f, 0x77, 0x31, 0xc0);
    CHECK(!faultop_match(&emms, &want));
    struct faultop last_byte = op(4, 0x0f, 0xef, 0xc1, 0x00);       /* pxor %mm1, %mm0 */
    CHECK(!faultop_match(&last_byte, &want));
    struct faultop first_byte = op(4, 0x1f, 0xef, 0xc0, 0x00);
    CHECK(!faultop_match(&first_byte, &want));
}

static void test_short_and_unreadable(void)
{
    struct faultop want = op(3, 0x0f, 0x01, 0x15, 0);               /* lgdt rip-relative */
    struct faultop unreadable = op(0, 0, 0, 0, 0);
    CHECK(!faultop_match(&unreadable, &want));
    struct faultop shorter = op(2, 0x0f, 0x01, 0, 0);               /* page ended after 2 bytes */
    CHECK(!faultop_match(&shorter, &want));
    struct faultop none = op(0, 0, 0, 0, 0);                        /* nothing expected: never a match */
    CHECK(!faultop_match(&want, &none));
    struct faultop bad_n = op(5, 0x0f, 0x01, 0x15, 0);
    CHECK(!faultop_match(&bad_n, &want));
    CHECK(!faultop_match(&want, &bad_n));
}

static void test_str(void)
{
    char buf[16];
    struct faultop a = op(4, 0x48, 0x8b, 0x04, 0x25);
    CHECK(strcmp(faultop_str(&a, buf), "48 8b 04 25") == 0);
    struct faultop b = op(1, 0xc3, 0, 0, 0);
    CHECK(strcmp(faultop_str(&b, buf), "c3") == 0);
    struct faultop c = op(0, 0, 0, 0, 0);
    CHECK(strcmp(faultop_str(&c, buf), "unreadable") == 0);
}

int main(void)
{
    printf("test_faultop\n");
    RUN(test_match);
    RUN(test_rejects_wrong_opcode);
    RUN(test_short_and_unreadable);
    RUN(test_str);
    return unit_report("test_faultop");
}
