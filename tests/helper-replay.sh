#!/usr/bin/env bash
# Replays every pattern-helper call of a finished tests/qemu-tests.sh run
# (build/helper-calls.trace) against the same logs under each awk given,
# with the helpers as they were up to 0.11 ("-v", escapes processed by
# awk) and as they are since 0.12 (ENVIRON, patterns passed unchanged).
# Prints one verdict line per call and implementation into
# build/replay-<awk>-<old|new>.txt and a summary. It decides no check.
#
#   bash tests/helper-replay.sh [--root DIR] [awk ...]   (default: mawk gawk)
# --root: the folder holding the run's build/ (0.12 stage 0c: one job's
# results in a sharded run); default: the repository root.
set -u
if [ "${1:-}" = "--root" ]; then cd "$2" || exit 1; shift 2
else cd "$(dirname "$0")/.."; fi
trace=build/helper-calls.trace
[ -s "$trace" ] || { echo "REPLAY: no $trace"; exit 1; }
impls=("$@")
[ ${#impls[@]} -gt 0 ] || impls=(mawk gawk)

# One call: prints the verdict (next: 0/1; inorder: the matching line
# number, 0 for none; width: the width). An awk error (an invalid regular
# expression, for example) is recorded as ERROR(<exit status>).
call() {
    local awk=$1 mode=$2 kind=$3 log=$4 a=$5 b=$6
    case "$mode:$kind" in
    old:next)
        "$awk" -v pat="$a" -v nxt="$b" '
            found && !/^key: / { ok = ($0 ~ nxt); exit }
            !found && $0 ~ pat { found = 1 }
            END { print ok + 0 }' "$log" 2>/dev/null ;;
    new:next)
        PAT="$a" NXT="$b" "$awk" '
            found && !/^key: / { ok = ($0 ~ ENVIRON["NXT"]); exit }
            !found && $0 ~ ENVIRON["PAT"] { found = 1 }
            END { print ok + 0 }' "$log" 2>/dev/null ;;
    old:inorder)
        "$awk" -v start="$a" -v pat="$b" 'NR > start && $0 ~ pat { n = NR; exit } END { print n + 0 }' "$log" 2>/dev/null ;;
    new:inorder)
        START="$a" PAT="$b" "$awk" 'NR > ENVIRON["START"] + 0 && $0 ~ ENVIRON["PAT"] { n = NR; exit } END { print n + 0 }' "$log" 2>/dev/null ;;
    old:width)
        "$awk" -v s="$a" -v e="$b" '
            !on && $0 ~ s { on = 1 }
            on { if (length($0) > m) m = length($0) }
            on && $0 ~ e && !($0 ~ s) { on = 0 }
            END { print m + 0 }' "$log" 2>/dev/null ;;
    new:width)
        S="$a" E="$b" "$awk" '
            !on && $0 ~ ENVIRON["S"] { on = 1 }
            on { if (length($0) > m) m = length($0) }
            on && $0 ~ ENVIRON["E"] && !($0 ~ ENVIRON["S"]) { on = 0 }
            END { print m + 0 }' "$log" 2>/dev/null ;;
    esac
}

for awk in "${impls[@]}"; do
    command -v "$awk" >/dev/null || { echo "REPLAY: $awk not installed"; exit 1; }
    for mode in old new; do
        out=build/replay-$awk-$mode.txt
        : > "$out"
        while IFS=$'\x1f' read -r kind log a b; do
            [ -f "$log" ] || { echo "$kind MISSING_LOG $log" >> "$out"; continue; }
            v=$(call "$awk" "$mode" "$kind" "$log" "$a" "$b") || v="ERROR($?)"
            printf '%s %s\n' "$kind" "$v" >> "$out"
        done < "$trace"
    done
done

echo "REPLAY_CALLS=$(wc -l < "$trace")"
for f in build/replay-*.txt; do
    echo "REPLAY_ERRORS $(basename "$f" .txt)=$(grep -c ' ERROR(' "$f")"
done
ref=build/replay-${impls[0]}-old.txt
for awk in "${impls[@]}"; do
    for mode in old new; do
        f=build/replay-$awk-$mode.txt
        d=$(diff "$ref" "$f" | grep -c '^>')
        echo "REPLAY $awk-$mode vs ${impls[0]}-old: $d differing verdicts"
    done
done
