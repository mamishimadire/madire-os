#!/usr/bin/env bash
# Madire OS: the owner's Kali trial (build and unit tests only, never evidence).
# The owner runs it by hand in the Kali VM's terminal on the old laptop
# (AGENTS.md sections 1 and 5), fetched and started with:
#   git clone --depth 1 -b wip/<version> https://github.com/mamishimadire/madire-os.git trial-tools
#   bash trial-tools/scripts/kali-trial.sh <full 40-character commit SHA>
# It creates a new folder under the current directory, fetches only that
# commit from the public GitHub repository (no token, push disabled),
# verifies it, builds it with gcc 15 (the Makefile's own -Werror flags):
# make iso, then every unit suite, and prints a result block. It installs
# nothing and deletes nothing; everything it writes is inside its new folder.
# The ISO's SHA-256 is informational only: it is not compared with Actions'
# (different gcc); only Actions' hash counts.
set -u
export LC_ALL=C

REPO_URL=https://github.com/mamishimadire/madire-os.git
SHA=${1:-}
if ! printf '%s\n' "$SHA" | grep -qxE '[0-9a-f]{40}'; then
    echo "usage: bash kali-trial.sh <full 40-character commit SHA>" >&2
    exit 2
fi

WORK="$PWD/madire-trial-${SHA:0:7}-$(date +%Y%m%d-%H%M%S)"
mkdir "$WORK" || { echo "cannot create $WORK" >&2; exit 2; }   # no -p: never reuse a folder
SRC="$WORK/src"
LOG="$WORK/logs"
mkdir "$SRC" "$LOG"

RESULT=PASS; REASON=""
fail() { RESULT=FAIL; REASON="${REASON:+$REASON; }$1"; }

# gcc 15: gcc itself if it is 15, else a gcc -> gcc-15 link inside the new
# folder, first on PATH (the Makefile calls plain gcc for the kernel and the
# unit tests).
GCC_MAJOR=$(gcc -dumpversion 2>/dev/null | cut -d. -f1)
if [ "$GCC_MAJOR" != 15 ]; then
    if command -v gcc-15 > /dev/null; then
        mkdir "$WORK/bin"
        ln -s "$(command -v gcc-15)" "$WORK/bin/gcc"
        export PATH="$WORK/bin:$PATH"
    else
        fail "gcc 15 not found (gcc is ${GCC_MAJOR:-missing})"
    fi
fi
GCC_VERSION=$(gcc --version 2>/dev/null | head -n 1)
[ "$(gcc -dumpversion 2>/dev/null | cut -d. -f1)" = 15 ] || fail "gcc on PATH is not 15"

# Prerequisites: checked only, never installed.
for tool in git make nasm ld objcopy objdump python3 xorriso limine sha256sum; do
    command -v "$tool" > /dev/null || fail "missing tool: $tool"
done
LIMINE_DIR=${LIMINE_DIR:-/usr/local/share/limine}
export LIMINE_DIR
for f in limine-bios.sys limine-bios-cd.bin LICENSE 3RDPARTY.md; do
    [ -f "$LIMINE_DIR/$f" ] || fail "missing $LIMINE_DIR/$f"
done

GOT_SHA="-"; SUBJECT="-"; ISO_SHA="-"
SUITES_FOUND=0; SUITES_RUN=0; SUITES_PASSED=0; WARNINGS="-"; WERROR="-"

if [ "$RESULT" = PASS ]; then
    # Only that commit (GitHub serves a commit by its full SHA); no
    # credentials, no push URL.
    if GIT_TERMINAL_PROMPT=0 git -C "$SRC" init -q \
       && git -C "$SRC" remote add origin "$REPO_URL" \
       && git -C "$SRC" remote set-url --push origin DISABLED-no-push \
       && GIT_TERMINAL_PROMPT=0 git -C "$SRC" -c credential.helper= \
              fetch -q --no-tags --depth 1 origin "$SHA" > "$LOG/fetch.log" 2>&1 \
       && git -C "$SRC" -c advice.detachedHead=false checkout -q FETCH_HEAD; then
        GOT_SHA=$(git -C "$SRC" rev-parse HEAD)
        SUBJECT=$(git -C "$SRC" log -1 --format=%s HEAD)
        [ "$GOT_SHA" = "$SHA" ] || fail "checked out $GOT_SHA, not $SHA"
        [ "$(git -C "$SRC" cat-file -t "$SHA")" = commit ] || fail "$SHA is not a commit"
        [ -z "$(git -C "$SRC" status --porcelain)" ] || fail "working tree differs from the commit"
    else
        fail "fetch or checkout failed (logs/fetch.log)"
    fi
fi

if [ "$RESULT" = PASS ]; then
    cd "$SRC" || fail "cannot enter the source folder $SRC"
fi

if [ "$RESULT" = PASS ]; then
    grep -q -- '-Werror' Makefile || fail "Makefile has no -Werror"

    # The ISO (kernel, user programs, Limine install), as on Actions; the
    # Makefile's ISO is $(BUILD)/madire-os.iso with BUILD := build.
    make iso > "$LOG/build-iso.log" 2>&1 || fail "make iso failed (logs/build-iso.log)"
    if [ -f build/madire-os.iso ]; then
        ISO_SHA=$(sha256sum build/madire-os.iso | cut -d' ' -f1)
    else
        fail "ISO not found (build/madire-os.iso)"
    fi

    # Every unit suite: build them all (-k: keep going), then run each one.
    SUITES=$(ls tests/unit/test_*.c 2>/dev/null | sed 's|tests/unit/\(.*\)\.c|build/unit/\1|')
    SUITES_FOUND=$(printf '%s\n' "$SUITES" | grep -c . || true)
    # shellcheck disable=SC2086
    make -k $SUITES > "$LOG/build-unit.log" 2>&1 || fail "unit build failed (logs/build-unit.log)"
    for t in $SUITES; do
        [ -x "$t" ] || { echo "SUITE NOT BUILT: $t" >> "$LOG/unit.log"; continue; }
        SUITES_RUN=$((SUITES_RUN + 1))
        echo "=== $t" >> "$LOG/unit.log"
        if "./$t" >> "$LOG/unit.log" 2>&1; then
            SUITES_PASSED=$((SUITES_PASSED + 1))
        else
            echo "SUITE FAILED: $t" >> "$LOG/unit.log"
        fi
    done
    [ "$SUITES_RUN" -eq "$SUITES_FOUND" ] || fail "unit suites built $SUITES_RUN of $SUITES_FOUND"
    [ "$SUITES_PASSED" -eq "$SUITES_RUN" ] || fail "unit suites failed: $((SUITES_RUN - SUITES_PASSED))"

    # gcc diagnostics in the build logs: "warning:" lines and -Werror lines
    # ("error: ... [-Werror=...]"), each listed in logs/diagnostics.txt.
    cat "$LOG"/build-*.log | grep -E 'warning:|-Werror' > "$LOG/diagnostics.txt" || true
    WARNINGS=$(cat "$LOG"/build-*.log | grep -c 'warning:' || true)
    WERROR=$(cat "$LOG"/build-*.log | grep -c '\[-Werror' || true)
    [ "$WARNINGS" = 0 ] || fail "gcc warnings: $WARNINGS"
    [ "$WERROR" = 0 ] || fail "-Werror lines: $WERROR"
fi
cd "$WORK" 2>/dev/null || true   # the result block is printed in any case

{
echo "=== MADIRE KALI TRIAL (not evidence) ==="
echo "commit:        $SHA"
echo "checked out:   $GOT_SHA"
echo "subject:       $SUBJECT"
echo "gcc:           ${GCC_VERSION:-missing}"
echo "result:        $RESULT${REASON:+ ($REASON)}"
echo "iso sha256:    $ISO_SHA (informational, not compared with Actions)"
echo "unit suites:   $SUITES_PASSED passed / $SUITES_RUN run / $SUITES_FOUND in tests/unit"
echo "warnings:      $WARNINGS"
echo "-Werror lines: $WERROR"
echo "folder:        $WORK"
echo "========================================"
} | tee "$WORK/RESULT.txt"

[ "$RESULT" = PASS ]
