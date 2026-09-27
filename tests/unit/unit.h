#ifndef MADIRE_TESTS_UNIT_H
#define MADIRE_TESTS_UNIT_H

/*
 * Minimal host-side unit test harness. Test programs are built with the host
 * compiler in 32-bit mode (`make unit`), linked against the kernel's pure
 * modules (no hardware access), and run in the build environment.
 */

#include <stdio.h>

static int unit_checks;
static int unit_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        unit_checks++;                                                       \
        if (!(cond)) {                                                       \
            unit_failures++;                                                 \
            printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                    \
    } while (0)

#define CHECK_EQ(actual, expected)                                           \
    do {                                                                     \
        unsigned long long a_ = (unsigned long long)(actual);                \
        unsigned long long e_ = (unsigned long long)(expected);              \
        unit_checks++;                                                       \
        if (a_ != e_) {                                                      \
            unit_failures++;                                                 \
            printf("    FAIL %s:%d: %s == 0x%llx, expected 0x%llx\n",       \
                   __FILE__, __LINE__, #actual, a_, e_);                     \
        }                                                                    \
    } while (0)

#define RUN(test)                                                            \
    do {                                                                     \
        printf("  %s\n", #test);                                             \
        test();                                                              \
    } while (0)

static inline int unit_report(const char *suite)
{
    printf("%s: %d checks, %d failed -> %s\n", suite, unit_checks, unit_failures,
           unit_failures ? "FAIL" : "PASS");
    return unit_failures ? 1 : 0;
}

#endif
