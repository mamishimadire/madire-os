# Madire OS QEMU tests, part static (0.12, stage 0c): the checks that boot
# nothing: Multiboot header, [0.8] reproducible build (needs make repro),
# [0.10] version read once. Sourced by tests/qemu-tests.sh (a full run, or
# --shard static). Apart from this header, every line was moved unchanged
# from the single script.

echo "== Multiboot header"
if grub-file --is-x86-multiboot "$KERNEL"; then pass "kernel has a valid Multiboot header"
else fail "kernel has no valid Multiboot header"; fi

# ---- 0.8: reproducible build ----

# `make repro` (run by `make test` before this script) built the kernel
# twice from clean, in two trees created in opposite file orders, and
# recorded the sha256 sums in build/repro/sha256.txt.
echo "== [0.8] Reproducible build (make repro)"
SUMS=$OUT/repro/sha256.txt
cat "$SUMS" 2>/dev/null | sed 's/^/      /'
sum_of() { F="$1" awk '$2 == ENVIRON["F"] { print $1 }' "$SUMS" 2>/dev/null; }
same_sum() {   # same_sum <description> <file> <file>
    local x y
    x=$(sum_of "$2"); y=$(sum_of "$3")
    if [ -n "$x" ] && [ "$x" = "$y" ]; then pass "$1 ($x)"
    else fail "$1 (${x:-missing} vs ${y:-missing})"; fi
}
same_sum "[0.8] madire.elf byte-identical in two clean builds" a/build/madire.elf b/build/madire.elf
same_sum "[0.8] madire.kernel byte-identical in two clean builds" a/build/madire.kernel b/build/madire.kernel
same_sum "[0.8] the kernel under test is the reproducible one" a/build/madire.kernel ../madire.kernel

echo "== [0.10] Version read once by the test harness"
if [ -n "$VERSION" ]; then pass "[0.10] version read from kernel/kernel.h: $VERSION (every version check uses it)"
else fail "[0.10] no version read from kernel/kernel.h"; fi
TITLE=$(grep -m1 -E '^menuentry "' "$OUT/grub.cfg")
if [ "$TITLE" = "menuentry \"Madire OS v$VERSION\" {" ]; then
    pass "[0.10] GRUB default entry is titled 'Madire OS v$VERSION'"
else
    fail "[0.10] GRUB default entry: '$TITLE', expected 'Madire OS v$VERSION'"
fi

