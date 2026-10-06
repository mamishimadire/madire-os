#!/bin/sh
# Build-time check (0.9): the boot screen (kernel/bootscreen.c) has no
# protection of its own. Once other threads may exist it must be used only
# by thread 0, with preemption off, through the BOOTSCREEN_SAFE() wrapper in
# kernel/kernel.c (which checks both). Direct bootscreen_*() calls are
# allowed only:
#   - in kernel/kernel.c, inside the boot sequence that runs before any
#     other thread can exist: kernel_main, start_devices, check_boot_modules;
#   - in kernel/devtest.c (protected): its timer and keyboard diagnostics run
#     at boot, one per boot, before any other thread exists.
# Exit status 1 (and the build stops) on any other call.
cd "$(dirname "$0")/.." || exit 1
status=0

others=$(grep -rlE 'bootscreen_[a-z_]+\(' kernel drivers |
         grep -vE '^kernel/(bootscreen\.[ch]|kernel\.c|devtest\.c)$')
if [ -n "$others" ]; then
    for f in $others; do
        echo "error: $f calls bootscreen_*(); use BOOTSCREEN_SAFE() in kernel/kernel.c"
    done
    status=1
fi

awk '
    # A function definition starts in column 0 with its return type and has
    # "(" but no ";". Its name is the first identifier before a "(" that is
    # not __attribute__. It ends at a "}" in column 0.
    /^[a-z]/ && /\(/ && !/;/ {
        line = $0
        while (match(line, /[A-Za-z_][A-Za-z_0-9]*\(/)) {
            name = substr(line, RSTART, RLENGTH - 1)
            line = substr(line, RSTART + RLENGTH)
            if (name != "__attribute__") { fn = name; break }
        }
    }
    /^}/ { fn = "" }
    /bootscreen_[a-z_]+\(/ && !/BOOTSCREEN_SAFE\(/ && !/^#/ {
        if (fn != "kernel_main" && fn != "start_devices" && fn != "check_boot_modules") {
            printf "error: kernel/kernel.c:%d: bootscreen_*() called directly in %s(); use BOOTSCREEN_SAFE()\n", NR, (fn == "" ? "file scope" : fn)
            bad = 1
        }
    }
    END { exit bad }
' kernel/kernel.c || status=1

if [ "$status" -eq 0 ]; then
    echo "Boot screen check: bootscreen_*() only in the boot sequence, devtest.c and BOOTSCREEN_SAFE()"
fi
exit "$status"
