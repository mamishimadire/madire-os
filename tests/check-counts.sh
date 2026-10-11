#!/usr/bin/env bash
# Sums the "SHARD_COUNTS ..." lines on stdin (one from a full run, or one per
# part of a sharded run) and compares the checks per tag with
# tests/expected-checks.txt (0.12, stage 0c). Prints the totals and
# EXPECTED_CHECKS=MATCH or MISMATCH; exits 1 on a mismatch or a failure.
#
#   ... | bash tests/check-counts.sh [expected-checks.txt]
set -u
expected=${1:-$(dirname "$0")/expected-checks.txt}
awk -v want_file="$expected" '
    BEGIN {
        while ((getline l < want_file) > 0) {
            if (l ~ /^#/ || l ~ /^[ \t]*$/) continue
            split(l, w, " ")
            if (w[1] == "unit-suites") continue
            ntag++; tag[ntag] = w[1]; want[w[1]] = w[2] + 0
        }
    }
    $1 == "SHARD_COUNTS" {
        lines++
        for (i = 2; i <= NF; i++) {
            split($i, kv, "=")
            if (kv[1] == "part") { parts = parts (parts ? " " : "") kv[2]; continue }
            got[kv[1]] += kv[2]
        }
    }
    END {
        bad = 0
        if (lines == 0) { print "EXPECTED_CHECKS=MISMATCH (no SHARD_COUNTS line)"; exit 1 }
        sum = 0
        for (i = 1; i <= ntag; i++) {
            t = tag[i]; sum += got[t]
            if (got[t] != want[t]) { printf "  %s: %d checks, expected %d\n", t, got[t], want[t]; bad = 1 }
        }
        printf "COUNTED parts=%s passes=%d failures=%d\n", parts, got["passes"], got["failures"]
        if (sum != got["passes"]) { printf "  tags add up to %d, passes %d\n", sum, got["passes"]; bad = 1 }
        if (got["failures"] != 0) { printf "  %d failed check(s)\n", got["failures"]; bad = 1 }
        if (bad) { print "EXPECTED_CHECKS=MISMATCH"; exit 1 }
        printf "EXPECTED_CHECKS=MATCH (%d checks)\n", sum
    }'
