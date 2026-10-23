# Terminus 4.49.1 (pinned BDF files)

The bitmap font of Madire OS's framebuffer console (1.2 stage 6, decision 8 of [docs/plans/1.2.md](../../docs/plans/1.2.md)). An asset under AGENTS.md section 11, item 6; listed in [THIRD_PARTY_ASSETS.md](../../THIRD_PARTY_ASSETS.md) and [THIRD_PARTY_NOTICES](../../THIRD_PARTY_NOTICES).

| Item | Value |
|---|---|
| Upstream | https://terminus-font.sourceforge.net/ (project files: https://sourceforge.net/projects/terminus-font/files/terminus-font-4.49/) |
| Package | `terminus-font-4.49.1.tar.gz`, downloaded on 2026-10-23 from https://downloads.sourceforge.net/project/terminus-font/terminus-font-4.49/terminus-font-4.49.1.tar.gz |
| Package SHA-256 | `d961c1b781627bf417f9b340693d64fc219e0113ad3a3af1a3424c7aa373ef79` (equal to the SHA-256 SourceForge publishes for the file; its SHA-1 `4b8656f7bb34fe3f729774d06a94d4b204de3c21` equal too) |
| Licence | SIL Open Font License 1.1 (the package's `OFL.TXT`; its `README`, section 7.1: "Terminus Font is licensed under the SIL Open Font License, Version 1.1"; every BDF's `NOTICE` property says the same) |
| Copyright | "Copyright (C) 2020 Dimitar Toshkov Zhekov, with Reserved Font Name "Terminus Font"." (`OFL.TXT`) |

**Files taken from the package, unmodified:** the eight BDF files below (regular `n` and bold `b`, at 8×16, 10×20, 12×24 and 16×32). **`OFL.txt`** is the package's `OFL.TXT` with its CRLF line endings changed to LF (AGENTS.md section 8); its text is unchanged. Nothing else was taken: the package's configure files and its Python and JavaScript sources are under the GPL (its `README`, section 7.1) and are not used. Madire's own `scripts/make-font.py` converts the BDF files to C at build time.

| File | Size | SHA-256 |
|---|---|---|
| `ter-u16n.bdf` (8×16 regular) | 181013 | `5197662b22bf9f3e68d4af9f969a7fefa3edae40dd82ae969a147381130fb4ae` |
| `ter-u16b.bdf` (8×16 bold) | 181009 | `92861fecd6f94c5ecc764fc43df5a69eb000ae13a75f652c6525db0495274f0a` |
| `ter-u20n.bdf` (10×20 regular) | 254241 | `b3868699145f51f1c059b955110c35db990d10a07536995cbcf7a4a901f82178` |
| `ter-u20b.bdf` (10×20 bold) | 254237 | `3051598391e5f7f59f09dc5aa26a97700fcbbe57ac315495878c52fc74aba500` |
| `ter-u24n.bdf` (12×24 regular) | 281361 | `ff640e9e097983355e8f70ed8fb645bd850184a0d590de17f6fc9ceec1bf8eaf` |
| `ter-u24b.bdf` (12×24 bold) | 281357 | `759e0dbbd137d0132cee5d83b681cea1c8a656f161e04f83e121525ea4eb1824` |
| `ter-u32n.bdf` (16×32 regular) | 335601 | `5ad01972d58adfee75077e2c65ba5947e260043031664cb8ad8f98b5473b23a4` |
| `ter-u32b.bdf` (16×32 bold) | 335597 | `f6295e92ebbf95afbb6274f4621608d255c1287ec90d3fefbc50f077bf09dab4` |
| `OFL.txt` (LF) | 4386 | `29e51260692fca30c51c3c81c5aa12205cfaa1be7180bf5e7c8192e745e78308` (the package's `OFL.TXT`, CRLF, 4480 bytes: `c14f8d795784a547ea35e69c51dee2957bb71a1cdb492ec5321e4b61d3d97630`) |

`SHA256SUMS` holds the same hashes; `scripts/make-font.py` refuses a BDF file whose hash differs.

**The name.** The converted font is a Modified Version under the OFL (only the 256 glyphs of code page 437 are kept, as C arrays), so it never uses the Reserved Font Name: no Madire user interface text, boot line or file shipped in the kernel names it "Terminus Font" (a part static check: no "Terminus" in `madire.kernel` or `limine.conf`).
