#!/usr/bin/env bash
# Installs the Madire OS build toolchain on Ubuntu/Debian (Codespace, WSL, CI).
# .devcontainer/devcontainer.json runs it when a Codespace is created.
#   build-essential  gcc (64-bit freestanding kernel, 64-bit host unit tests), ld, make
#   qemu-system-x86  qemu-system-x86_64, and qemu-system-i386 for the "no 64-bit CPU" test
#   python3          tests/memory_check.py, tests/qmp_keys.py, scripts/logo-preview.py
set -euo pipefail

sudo apt-get update
sudo apt-get install -y --no-install-recommends \
    build-essential nasm python3 \
    grub-pc-bin grub-common xorriso mtools \
    qemu-system-x86
