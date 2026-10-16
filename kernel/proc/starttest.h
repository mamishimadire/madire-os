#ifndef MADIRE_PROC_STARTTEST_H
#define MADIRE_PROC_STARTTEST_H

/*
 * The System V start-up diagnostic (1.0, stage 7), a QEMU boot option only
 * (plan D7). test=startup runs the program args (its own report of what it
 * received, and its checks of its start block) as a built-in image and as
 * a program module (the boot needs args.elf as a module), with arguments
 * and an environment, with none, with an empty and a 255-byte argument,
 * with the largest start block (4096 bytes) and one byte more (E2BIG,
 * nothing created), through a numeric start (argv { "args", "7" }), and
 * killed before ring 3 (every start block freed, the heap back to where
 * it was).
 */

#include <stdbool.h>

bool starttest_startup(void);

#endif
