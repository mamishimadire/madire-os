#!/usr/bin/env bash
# A fake QEMU for part static's boot_until self-test (1.3 stage 7a). The
# self-test runs it as QEMU_REAL, so it is started as a real boot is:
# timeout -> the qemu-system-x86_64 wrapper -> tests/qemu-watch.sh -> setsid.
# It needs no QEMU, KVM or kernel: it only writes lines to the file given
# as "-serial file:<log>" and ignores every other argument. The behaviour
# comes from BOOT_UNTIL_FAKE:
#   term    writes "System ready.", then a line every 0.2 s; SIGTERM ends it
#   ignore  the same, but ignores SIGTERM (only SIGKILL ends it)
#   limit   never writes "System ready."; a line every 0.2 s until stopped
#   exited  never writes "System ready."; ends by itself after about 0.5 s
set -u
log=""
prev=""
for a in "$@"; do
    case "$prev:$a" in
    -serial:file:*) log=${a#file:} ;;
    esac
    prev=$a
done
[ -n "$log" ] || exit 2
mode=${BOOT_UNTIL_FAKE:-}
[ "$mode" = ignore ] && trap '' TERM
echo "fake boot ($mode)" >> "$log"
case "$mode" in
term|ignore) echo "System ready." >> "$log" ;;
exited) sleep 0.5; echo "fake end" >> "$log"; exit 0 ;;
limit) ;;
*) exit 2 ;;
esac
while :; do
    echo "fake tick" >> "$log"
    sleep 0.2
done
