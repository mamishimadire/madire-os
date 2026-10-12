#!/usr/bin/env bash
# Installs the Madire OS build toolchain on Ubuntu/Debian (Codespace, WSL, CI).
# .devcontainer/devcontainer.json runs it when a Codespace is created.
#   build-essential  gcc (64-bit freestanding kernel, 64-bit host unit tests), ld, make
#   qemu-system-x86  qemu-system-x86_64, and qemu-system-i386 for the "no 64-bit CPU" test
#   python3          tests/memory_check.py, tests/qmp_keys.py, scripts/logo-preview.py
#   curl, xz-utils   (0.13) fetching and unpacking the pinned Limine package
set -euo pipefail

sudo apt-get update
sudo apt-get install -y --no-install-recommends \
    build-essential nasm python3 \
    xorriso mtools \
    qemu-system-x86 \
    ca-certificates curl xz-utils

# 0.13: Limine, the separate boot tool (AGENTS.md section 11, item 5): the
# unmodified binary package of a pinned version, checked against its pinned
# SHA-256 on every fetch (its signature was verified by hand once, in 0.13
# stage 1). Only the host tool is built, from the package's own limine.c.
# The BIOS files and the notices go to $LIMINE_DIR, the tool to /usr/local/bin.
LIMINE_VERSION=12.9.3
LIMINE_SHA256=e02a473c6dc6df25159697f94930272397f21fd86e5d4eeee61bba72c95ec8d2
LIMINE_URL=https://github.com/Limine-Bootloader/Limine/releases/download/v$LIMINE_VERSION/limine-binary.tar.xz
LIMINE_DIR=/usr/local/share/limine

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
curl -fsSL --retry 3 -o "$tmp/limine-binary.tar.xz" "$LIMINE_URL"
echo "$LIMINE_SHA256  $tmp/limine-binary.tar.xz" | sha256sum -c -
tar -xJf "$tmp/limine-binary.tar.xz" -C "$tmp"
make -C "$tmp/limine-binary" limine
sudo install -d "$LIMINE_DIR"
sudo install -m 0644 "$tmp/limine-binary/limine-bios.sys" "$tmp/limine-binary/limine-bios-cd.bin" \
    "$tmp/limine-binary/LICENSE" "$tmp/limine-binary/3RDPARTY.md" "$LIMINE_DIR/"
echo "$LIMINE_VERSION" | sudo tee "$LIMINE_DIR/VERSION" > /dev/null
sudo install -m 0755 "$tmp/limine-binary/limine" /usr/local/bin/limine
limine --version | head -n 1
