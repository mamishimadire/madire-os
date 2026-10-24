#!/usr/bin/env bash
# Section timings (1.3 stage 1; report only: decides no check, adds none to
# the counts, never fails). Reads the "SECTION_SECONDS" lines of one or more
# regression logs (tests/qemu-tests.sh prints them; one log per job of a
# sharded run, or one full run's log) and prints, per part, the part's total
# and a table of its sections (part, section, seconds), the longest first.
# Each "SECTION_SECONDS <part> <n> <s>" is paired with the part's n-th "== "
# line in the same log; n = 0 is the part's start before its first section.
# A part whose numbers and titles do not pair up is reported as such.
#
#   bash tests/section-times.sh <regression.log>...
set -u
for log in "$@"; do
    [ -f "$log" ] || { echo "SECTION_TIMES no log $log"; continue; }
    awk '
        function flush(   i, j, t, order, nrows) {
            if (part == "") return
            nrows = 0
            for (i = 0; i <= n; i++) {
                if (!(i in secs)) continue
                nrows++; order[nrows] = i
            }
            for (i = 2; i <= nrows; i++) {       # insertion sort, longest first (mawk has no asort)
                t = order[i]
                for (j = i - 1; j >= 1 && secs[order[j]] + 0 < secs[t] + 0; j--) order[j + 1] = order[j]
                order[j + 1] = t
            }
            printf "SECTION_TIMES part=%s sections=%d timed=%d total=%s s\n", part, n, nrows - (0 in secs), total
            if (nrows != n + 1 || total == "") printf "SECTION_TIMES part=%s: %d titles, %d timings, total %s (they do not pair up)\n", part, n, nrows, (total == "" ? "missing" : total)
            print "| part | section | seconds |"
            print "|---|---|---|"
            for (i = 1; i <= nrows; i++) {
                t = order[i]
                printf "| %s | %s | %s |\n", part, (t == 0 ? "(the part'"'"'s start, before its first section)" : title[t]), secs[t]
            }
            part = ""; n = 0; total = ""
            split("", secs); split("", title)
        }
        $1 == "SHARD_PART_BEGIN" { flush(); part = $2; n = 0; total = ""; next }
        part != "" && /^== / { n++; title[n] = substr($0, 4); next }
        part != "" && $1 == "SECTION_SECONDS" && $2 == part {
            if ($3 == "total") total = $4; else secs[$3 + 0] = $4
            next
        }
        $1 == "SHARD_PART_END" { flush() }
        END { flush() }' "$log"
done
