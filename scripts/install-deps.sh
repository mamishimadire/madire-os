#!/usr/bin/env bash
# Installs the Madire OS build toolchain on Ubuntu/Debian (Codespace, WSL, CI).
set -euo pipefail

sudo apt-get update
sudo apt-get install -y --no-install-recommends \
    build-essential gcc-multilib nasm \
    grub-pc-bin grub-common xorriso mtools \
    qemu-system-x86
