#!/bin/sh
# Madire OS 1.2 stage 3: makes a probe ISO (test-only; docs/plans/1.2.md,
# stage 3) from the probe kernel in <build>/probe (make probe):
#
#   sh tests/probe-iso.sh <build> <first case> <out.iso>
#
# The ISO holds the probe kernel in six header variants (tests/probe_fb.py
# patch) and a limine.conf made of the real one's global options (from
# <build>/limine.conf, so a shown menu looks the same) and the seven probe
# cases below as entries, <first case> first: the entry Limine boots when
# no key is pressed. Limine's BIOS files and notices come from LIMINE_DIR,
# the wallpaper from <build>. tests/qemu-part-1.sh makes one ISO per case;
# make probe-iso makes case 1's for the VirtualBox probe. A probe ISO is
# never acceptance evidence.
set -eu
B=$1
FIRST=$2
OUTISO=$3
LIMINE=${LIMINE:-limine}
LIMINE_DIR=${LIMINE_DIR:-/usr/local/share/limine}
HERE=$(dirname "$0")
TREE=${OUTISO%.iso}.tree

rm -rf "$TREE" && mkdir -p "$TREE/boot/limine"
for m in 1280x800x32 0x0x0 1024x768x32 800x600x32 1920x1080x32 1280x800x24; do
    python3 "$HERE/probe_fb.py" patch "$B/probe/madire.kernel" "$TREE/boot/probe-$m.kernel" \
        $(echo "$m" | tr x ' ')
done

# entry <case>: the case's limine.conf entry.
entry() {
    x=""
    case $1 in
    1) t="video 1280x800x32 (delay, aux)"; k=1280x800x32; x="    cmdline: probedelay probeaux" ;;
    2) t="video 1280x800x32 with textmode: yes (delay)"; k=1280x800x32
       x="    textmode: yes
    cmdline: probedelay" ;;
    3) t="video, no preferred mode (0x0x0)"; k=0x0x0 ;;
    4) t="video 1024x768x32"; k=1024x768x32 ;;
    5) t="video 800x600x32"; k=800x600x32 ;;
    6) t="video 1920x1080x32"; k=1920x1080x32 ;;
    7) t="video 1280x800x24"; k=1280x800x24 ;;
    *) echo "probe-iso.sh: unknown case '$1'" >&2; exit 1 ;;
    esac
    printf '/Probe %s: %s\n    protocol: multiboot1\n    path: boot():/boot/probe-%s.kernel\n' "$1" "$t" "$k"
    if [ -n "$x" ]; then printf '%s\n' "$x"; fi
}

{
    echo "# Madire OS probe ISO (1.2 stage 3; test-only): the real global options, then the probe cases."
    awk '/^\// { exit } /^#/ { next } { print }' "$B/limine.conf"
    entry "$FIRST"
    for c in 1 2 3 4 5 6 7; do
        if [ "$c" != "$FIRST" ]; then entry "$c"; fi
    done
} > "$TREE/boot/limine/limine.conf"
cp "$LIMINE_DIR/limine-bios.sys" "$LIMINE_DIR/limine-bios-cd.bin" "$LIMINE_DIR/LICENSE" \
   "$LIMINE_DIR/3RDPARTY.md" "$B/wallpaper.png" "$TREE/boot/limine/"

# As tests/qemu-lib.sh's limine_iso (and make iso) make an ISO.
rm -f "$OUTISO"
SOURCE_DATE_EPOCH=1791763200 xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
    -no-emul-boot -boot-load-size 4 -boot-info-table --protective-msdos-label -partition_offset 16 \
    -o "$OUTISO" "$TREE" > /dev/null 2>&1
"$LIMINE" bios-install "$OUTISO" > /dev/null 2>&1
echo "probe ISO $OUTISO: case $FIRST first, $(grep -c '^/Probe ' "$TREE/boot/limine/limine.conf") entries, sha256 $(sha256sum < "$OUTISO" | cut -d' ' -f1)"
