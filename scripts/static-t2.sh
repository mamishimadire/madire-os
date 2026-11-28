#!/bin/bash
# scripts/static-t2.sh <source folder>: T2 for part static's 1.5 stage 14b
# checks (never evidence; the owner runs it by hand in the Kali VM after
# scripts/kali-trial.sh, on the folder that script printed, its src/).
# It builds build/pointerdump and build/desktopdump there, extracts the
# section "[1.5] The v1.5 pointer set, ..." from tests/qemu-part-static.sh
# unchanged except its mode and scene lists (one mode, 1024x768x32, and one
# scene, files_band), and runs it three times in three new folders, each with
# wrappers in place of the two tools:
#   good    the tools' own output;
#   broken  one cell of the hand pointer's 100 % sprite changed, and one
#           byte of the files_band picture changed;
#   anchor  the hand pointer's 100 % block header removed, and the
#           files_band picture missing.
# Expected: good passes the hand and files_band checks; broken and anchor
# fail both. It installs and deletes nothing outside the new folders.
set -u
SRC=${1:?usage: bash static-t2.sh <source folder>}
cd "$SRC" || { echo "static-t2: cannot enter $SRC" >&2; exit 2; }
[ -f tests/qemu-part-static.sh ] || { echo "static-t2: $SRC is not a Madire OS source folder" >&2; exit 2; }
if command -v gcc-15 > /dev/null && ! gcc --version | head -n 1 | grep -q ' 15\.'; then
    mkdir -p build/t2bin && ln -sf "$(command -v gcc-15)" build/t2bin/gcc && export PATH="$PWD/build/t2bin:$PATH"
fi
make build/pointerdump build/desktopdump > build/static-t2-make.log 2>&1 ||
    { echo "static-t2: the host tools did not build (build/static-t2-make.log)" >&2; exit 1; }
T2=$PWD/build/static-t2-$(date +%Y%m%d-%H%M%S)
mkdir "$T2" || exit 2                   # no -p: never reuse a folder
sed -n '/^section "\[1\.5\] The v1\.5 pointer set, This PC, box selection/,/^echo "      \[1\.5\] the S14 static section: /p' \
    tests/qemu-part-static.sh |
    sed 's/^for mode in "800 600 4" .*; do$/for mode in "1024 768 4"; do/;
         s/^    for scene in files_thispc files_thispc_cd .*; do$/    for scene in files_band; do/' > "$T2/section.sh"
grep -q '^for mode in "1024 768 4"; do$' "$T2/section.sh" && grep -q '^    for scene in files_band; do$' "$T2/section.sh" ||
    { echo "static-t2: the section's mode or scene list was not found" >&2; exit 1; }
REAL_P=$PWD/build/pointerdump
REAL_D=$PWD/build/desktopdump
for c in good broken anchor; do
    mkdir "$T2/$c"
    cat > "$T2/$c/pointerdump" <<EOF
#!/bin/bash
case $c in
good) exec "$REAL_P" ;;
broken) "$REAL_P" | awk 'f == 1 { c = substr(\$0, 1, 1); \$0 = (c == "." ? "X" : ".") substr(\$0, 2); f = 2 }
                          /^KIND hand scale 4 / { f = 1 } { print }' ;;
anchor) "$REAL_P" | grep -v '^KIND hand scale 4 ' ;;
esac
EOF
    cat > "$T2/$c/desktopdump" <<EOF
#!/bin/bash
"$REAL_D" "\$@"
rc=\$?
case $c in
broken) python3 -c 'import sys; p = sys.argv[1]; b = bytearray(open(p, "rb").read()); i = len(b) // 2; b[i] ^= 0x40; open(p, "wb").write(b)' "\$5" ;;
anchor) rm -f "\$5" ;;
esac
exit \$rc
EOF
    chmod +x "$T2/$c/pointerdump" "$T2/$c/desktopdump"
    (
        OUT=$T2/$c
        DREF=$PWD/tests/desktop_ref.py
        section() { :; }
        pass() { echo "PASS $1"; }
        fail() { echo "FAIL $1"; }
        # shellcheck disable=SC1091
        . "$T2/section.sh"
    ) > "$T2/$c/checks.txt" 2>&1
done
echo "static-t2 result (never evidence): $T2"
for c in good broken anchor; do
    h=$(grep -E '^(PASS|FAIL) \[1\.5\] the pointer hand' "$T2/$c/checks.txt" | cut -c1-4)
    s=$(grep -E '^(PASS|FAIL) \[1\.5\] (the scene files_band at|the files_band scene at)' "$T2/$c/checks.txt" | cut -c1-4)
    echo "case $c: pointer hand ${h:-NONE}, scene files_band 1024x768x32 ${s:-NONE}"
done
G=$(grep -cE '^PASS \[1\.5\] (the pointer hand|the scene files_band)' "$T2/good/checks.txt")
B=$(grep -cE '^FAIL \[1\.5\] (the pointer hand|the files_band scene)' "$T2/broken/checks.txt")
A=$(grep -cE '^FAIL \[1\.5\] (the pointer hand|the files_band scene)' "$T2/anchor/checks.txt")
if [ "$G" -eq 2 ] && [ "$B" -eq 2 ] && [ "$A" -eq 2 ]; then
    echo "STATIC T2: PASS (good passes both; broken and anchor fail both)"
else
    echo "STATIC T2: FAIL (good $G/2 passed, broken $B/2 failed, anchor $A/2 failed; see $T2/*/checks.txt)"
fi
