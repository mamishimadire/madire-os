# Third-party assets in Madire OS

Assets (images, fonts, sounds and the like) that Madire OS ships and did not create itself, with each one's licence (AGENTS.md section 11, item 6). Created in 1.2 stage 6, with the first such asset. Third-party software is listed in [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES). This file is at every ISO's root.

## Terminus 4.49.1 (the console font, since 1.2)

| Item | Value |
|---|---|
| What | The bitmap font Terminus, version 4.49.1, by Dimitar Toshkov Zhekov: https://terminus-font.sourceforge.net/ |
| Licence | SIL Open Font License, Version 1.1 (OFL-1.1). The full text: [third_party/terminus/OFL.txt](third_party/terminus/OFL.txt), also at `/third_party/terminus/OFL.txt` on every ISO |
| Copyright | Copyright (C) 2020 Dimitar Toshkov Zhekov, with Reserved Font Name "Terminus Font" |
| Files kept | Eight BDF files of the release package `terminus-font-4.49.1.tar.gz`, unmodified, in `third_party/terminus/` (8×16, 10×20, 12×24 and 16×32, regular and bold), with their SHA-256 ([third_party/terminus/README.md](third_party/terminus/README.md)) |
| How it ships | Converted at build time by Madire's own `scripts/make-font.py` to C arrays of the 256 glyphs of code page 437, linked into the kernel (`madire.kernel`); the regular weight at every size so far. The framebuffer console draws with it |
| Modified Version | The converted font keeps only some glyphs and changes the format, so under the OFL it is a Modified Version: it does not use the Reserved Font Name; no Madire user interface text or boot line calls it "Terminus Font" |
| Not taken | The package's configure files and its Python and JavaScript sources (GPL-2.0-or-later, the package's README, section 7.1) are not used and not kept |
