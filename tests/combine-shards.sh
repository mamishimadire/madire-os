#!/usr/bin/env bash
# Combines a sharded regression (0.12, stage 0c): the jobs of
# .github/workflows/regression.yml each leave a folder <dir>/shard-<part>
# (commit.txt, make-exit.txt, hashes.txt, regression.log, build/ logs and
# the helper trace; the static one also checks.txt). This script, run from
# the repository root of the same commit:
#   - checks every job tested the same commit and built the same madire.elf
#     and madire.kernel, and reports whether the ISO is identical too;
#   - checks every part ran exactly once, in its own job, to its end;
#   - runs the part "final" over every job's serial logs;
#   - checks the unit suites and make repro ran once (static job) and the
#     static checks (symbols, libgcc/libc, SSE/x87, the kernel margin:
#     1.0 stage 1, below 4 MiB, computed here from KERNEL_END_VIRTUAL);
#   - sums the counts and compares them with tests/expected-checks.txt;
#   - reports every job's make-step seconds and every section's seconds
#     (tests/section-times.sh; report only, 1.3 stage 1);
#   - replays every pattern-helper call under mawk and gawk
#     (tests/helper-replay.sh; evidence only, decides no check);
#   - writes <dir>/regression.log (every job's log, then the summary).
# Exits 0 only if everything passed.
#
#   bash tests/combine-shards.sh <dir>
set -u
dir=$1
here=$(cd "$(dirname "$0")" && pwd)
shards=$(bash "$here/qemu-tests.sh" --list-parts | sed 's/ final$//; s/ final / /')
ok=1
bad() { echo "COMBINE_FAIL: $*"; ok=0; }
summary=$dir/combine-summary.txt
main() {
    echo "== Jobs"
    for s in $shards; do
        [ -d "$dir/shard-$s" ] || { bad "no results from job shard-$s"; continue; }
        echo "shard-$s: commit $(cat "$dir/shard-$s/commit.txt" 2>/dev/null), make exit $(cat "$dir/shard-$s/make-exit.txt" 2>/dev/null)"
        [ "$(cat "$dir/shard-$s/make-exit.txt" 2>/dev/null)" = 0 ] || bad "shard-$s: make exit not 0"
    done
    commits=$(cat "$dir"/shard-*/commit.txt 2>/dev/null | awk '!seen[$0]++')
    if [ "$(wc -l <<< "$commits")" -eq 1 ] && [ -n "$commits" ]; then echo "SAME_COMMIT=$commits"
    else bad "the jobs tested different commits: $(tr '\n' ' ' <<< "$commits")"; fi
    manifests=$(for s in $shards; do sha256sum < "$dir/shard-$s/manifest.sha256" 2>/dev/null; done | awk '!seen[$0]++')
    if [ "$(wc -l <<< "$manifests")" -eq 1 ] && [ -n "$manifests" ]; then
        echo "SAME_FILES_UNDER_TEST=$(wc -l < "$dir/shard-static/manifest.sha256") files, manifest ${manifests%% *}"
    else bad "the jobs' manifests of the files under test differ"; fi
    for s in $shards; do
        grep -qx 'FINAL_SNAPSHOT_HASH_MATCH=PASS' "$dir/shard-$s/checks.txt" 2>/dev/null ||
            bad "shard-$s: the files under test changed during the run (or no check)"
    done

    echo "== Builds"
    for f in madire.elf madire.kernel madire-os.iso; do
        sums=$(grep -h " build/$f\$" "$dir"/shard-*/hashes.txt 2>/dev/null | awk '{ print $1 }' | awk '!seen[$0]++')
        n=$(grep -h " build/$f\$" "$dir"/shard-*/hashes.txt 2>/dev/null | wc -l)
        if [ "$f" = madire-os.iso ]; then
            if [ "$(wc -l <<< "$sums")" -eq 1 ]; then echo "ISO_IDENTICAL_IN_ALL_JOBS=yes ($n builds, $sums)"
            else echo "ISO_IDENTICAL_IN_ALL_JOBS=no ($(wc -l <<< "$sums") different in $n builds; reported, not required)"; fi
            continue
        fi
        if [ "$n" -eq "$(wc -w <<< "$shards")" ] && [ "$(wc -l <<< "$sums")" -eq 1 ]; then echo "SAME_${f//[.-]/_}=$sums ($n builds)"
        else bad "$f differs between jobs or is missing ($n hashes: $(tr '\n' ' ' <<< "$sums"))"; fi
    done

    echo "== Parts"
    for s in $shards; do
        log=$dir/shard-$s/regression.log
        begun=$(grep -c '^SHARD_PART_BEGIN ' "$log" 2>/dev/null)
        if [ "$begun" = 1 ] && grep -qx "SHARD_PART_BEGIN $s" "$log" && grep -qx "SHARD_PART_END $s" "$log"; then
            echo "part $s: ran once, in job shard-$s, to its end"
        else
            bad "part $s: not run exactly once to its end in job shard-$s"
        fi
        # Every serial log the part names must have been made in its own job
        # (a log made by another part would be missing in a sharded run).
        for f in $(grep -oE 'serial-[A-Za-z0-9_-]+\.log' "$here/qemu-part-$s.sh" | awk '!seen[$0]++'); do
            [ -f "$dir/shard-$s/build/$f" ] || bad "part $s reads $f, which its job did not make"
        done
    done

    echo "== Part final (every job's serial logs)"
    comb=$dir/combined/build
    rm -rf "$dir/combined" && mkdir -p "$comb"
    for s in $shards; do
        for f in "$dir/shard-$s"/build/serial-*.log; do
            [ -f "$f" ] || continue
            b=$(basename "$f" .log)
            if [ -e "$comb/$b.log" ]; then cp "$f" "$comb/$b.shard-$s.log"; else cp "$f" "$comb/$b.log"; fi
        done
    done
    echo "$(ls "$comb" | wc -l) serial logs gathered"
    bash "$here/qemu-tests.sh" "$comb/madire.kernel" "$comb/madire-os.iso" --shard final > "$dir/combined/final.log" 2>&1
    final_rc=$?
    grep -E '^(PASS|FAIL)|^SHARD ' "$dir/combined/final.log"
    [ "$final_rc" -eq 0 ] || bad "part final failed"

    echo "== Unit suites, make repro, static checks (job shard-static)"
    want_units=$(awk '$1 == "unit-suites" { print $2 }' "$here/expected-checks.txt")
    units=$(grep -c -- '-> PASS$' "$dir/shard-static/regression.log" 2>/dev/null)
    elsewhere=$(cat "$dir"/shard-[!s]*/regression.log 2>/dev/null | grep -c -- '-> PASS$')
    echo "UNIT_SUITES_PASS=$units (expected $want_units; in other jobs: $elsewhere)"
    [ "$units" = "$want_units" ] && [ "$elsewhere" -eq 0 ] || bad "unit suites: $units in shard-static, $elsewhere elsewhere, expected $want_units once"
    repro=$(grep -h '^Reproducible build: ' "$dir"/shard-*/regression.log 2>/dev/null)
    echo "$repro"
    [ "$repro" = "Reproducible build: identical" ] || bad "make repro did not run once with an identical result"
    chk=$dir/shard-static/checks.txt
    for k in UNDEFINED_SYMBOLS LIBGCC_LIBC_HELPERS SSE_X87_REGISTER_USES; do
        v=$(sed -n "s/^$k=//p" "$chk" 2>/dev/null)
        echo "$k=${v:-missing}"
        [ "$v" = 0 ] || bad "$k is ${v:-missing}, expected 0"
    done
    # 1.0 stage 1 (D1): the gate is the margin below 4 MiB (0x400000 - the
    # kernel's physical end, at least 32768 bytes); the 2 MiB figure is a
    # report only. Both are computed here from KERNEL_END_VIRTUAL, which the
    # workflow prints in every version: the workflow runs from main, so its
    # own KERNEL_MARGIN_BYTES line exists only once 1.0 is there; when it
    # is printed, it must equal the value computed here.
    end=$(sed -n 's/^KERNEL_END_VIRTUAL=0x\([0-9a-f]\{16\}\)$/\1/p' "$chk" 2>/dev/null | head -n 1)
    grep -E '^KERNEL_END_VIRTUAL=' "$chk" 2>/dev/null
    km=""; tm=""
    if [ -n "$end" ]; then
        km=$(( 0x400000 - (0x$end - 0xffffffff80000000) ))
        tm=$(( 0x200000 - (0x$end - 0xffffffff80000000) ))
    fi
    echo "KERNEL_MARGIN_BYTES=${km:-missing} (below 4 MiB; the gate: at least 32768)"
    echo "TWO_MIB_MARGIN_BYTES=${tm:-missing} (below 2 MiB; report only)"
    wm=$(sed -n 's/^KERNEL_MARGIN_BYTES=//p' "$chk" 2>/dev/null | head -n 1)
    if [ -n "$wm" ]; then
        echo "KERNEL_MARGIN_BYTES printed by the workflow: $wm"
        [ "$wm" = "$km" ] || bad "the workflow's KERNEL_MARGIN_BYTES $wm differs from the computed ${km:-missing}"
    else
        echo "KERNEL_MARGIN_BYTES not printed (workflow runs from main)"
    fi
    [ -n "$km" ] && [ "$km" -ge 32768 ] || bad "kernel margin below 4 MiB ${km:-missing} below 32768 bytes"

    # 1.2 stage 2 (decision 13): the size report of part static, repeated
    # here, and the margin computed above against the 524,288-byte trigger
    # for a size-gate stage. Report only: decides nothing.
    echo "== Size report (job shard-static; report only)"
    grep -E '^(SIZE|KERNEL_MARGIN_TRIGGER) ' "$dir/shard-static/regression.log" 2>/dev/null ||
        echo "no size report in shard-static's log"
    if [ -n "$km" ] && [ "$km" -ge 524288 ]; then echo "KERNEL_MARGIN_TRIGGER (combined) $km, trigger 524288: at or above the trigger: no gate stage"
    else echo "KERNEL_MARGIN_TRIGGER (combined) ${km:-missing}, trigger 524288: BELOW the trigger or missing: STOP for a gate stage"; fi

    # 1.3 stage 1: every section's seconds, per part (tests/section-times.sh).
    # Report only: decides nothing.
    echo "== Section timings (report only)"
    for s in $shards; do
        echo "job shard-$s: make step $(cat "$dir/shard-$s/duration.txt" 2>/dev/null || echo "seconds: missing")"
    done
    bash "$here/section-times.sh" $(for s in $shards; do echo "$dir/shard-$s/regression.log"; done) "$dir/combined/final.log"

    echo "== Counts"
    counts=$( { cat "$dir"/shard-*/regression.log; cat "$dir/combined/final.log"; } | grep '^SHARD_COUNTS ')
    echo "$counts"
    echo "$counts" | bash "$here/check-counts.sh" "$here/expected-checks.txt" || bad "the counts differ from tests/expected-checks.txt"
    summed=$(echo "$counts" | awk '{ for (i = 3; i <= NF; i++) { split($i, kv, "="); s[kv[1]] += kv[2] } }
        END { printf "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d", s["passes"], s["failures"], s["0.3"], s["0.4"], s["0.5"], s["0.6"], s["0.7"], s["0.8"], s["0.9"], s["0.9.1"], s["0.9.2"], s["0.10"], s["0.11"], s["0.12"], s["0.13"], s["1.0"], s["1.1"], s["1.2"], s["1.3"], s["1.4"] }')
    read -r passes failures c03 c04 c05 c06 c07 c08 c09 c091 c092 c010 c011 c012 c013 c10 c11 c12 c13 c14 <<< "$summed"

    echo "== Helper replay (evidence only; decides no check)"
    calls=0; declare -A diffs errs
    for s in $shards; do
        [ -s "$dir/shard-$s/build/helper-calls.trace" ] || continue
        out=$(bash "$here/helper-replay.sh" --root "$dir/shard-$s" mawk gawk)
        calls=$((calls + $(sed -n 's/^REPLAY_CALLS=//p' <<< "$out")))
        while read -r _ which _ _ n _; do diffs[$which]=$(( ${diffs[$which]:-0} + n )); done < <(grep '^REPLAY [a-z]*-[a-z]* vs' <<< "$out")
        while read -r _ kv; do k=${kv%%=*}; errs[$k]=$(( ${errs[$k]:-0} + ${kv#*=} )); done < <(grep '^REPLAY_ERRORS ' <<< "$out")
    done
    echo "REPLAY_CALLS=$calls"
    for k in mawk-old mawk-new gawk-old gawk-new; do
        echo "REPLAY $k vs mawk-old: ${diffs[$k]:-?} differing verdicts, ${errs[replay-$k]:-?} awk errors"
    done

    echo
    if [ "$ok" -eq 1 ]; then
        echo "ALL TESTS PASSED ($passes checks: $c03 0.3 regression + $c04 from 0.4 + $c05 from 0.5 + $c06 from 0.6 + $c07 from 0.7 + $c08 from 0.8 + $c09 from 0.9 + $c091 from 0.9.1 + $c092 from 0.9.2 + $c010 from 0.10 + $c011 from 0.11 + $c012 from 0.12 + $c013 from 0.13 + $c10 from 1.0 + $c11 from 1.1 + $c12 from 1.2 + $c13 from 1.3 + $c14 new in 1.4)"
        echo "COMBINED_RESULT=PASS"
    else
        echo "$failures TEST(S) FAILED; COMBINED_RESULT=FAIL"
    fi
    [ "$ok" -eq 1 ]
}

main > "$summary" 2>&1
rc=$?
cat "$summary"
{ for s in $shards; do echo "===== job shard-$s"; cat "$dir/shard-$s/regression.log" 2>/dev/null; done
  echo "===== part final (combining job)"; cat "$dir/combined/final.log" 2>/dev/null
  echo "===== combined summary"; cat "$summary"; } > "$dir/regression.log"
exit "$rc"
