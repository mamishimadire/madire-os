# Madire Design Guide v0.1

**Versions:** v0.1 (Madire OS 1.2, stage 1): colours, fonts, spacing and shapes, icons, layout and display scaling, the tagline. Written clean-room: from the owner's mockup (below) and Madire's own 1.1 artwork, with published human interface guidelines read for ideas only ([references.md](../references.md)).

This guide sets how Madire OS looks. Every graphical part of Madire follows it; a change to it is a design change, raised before it is implemented ([AGENTS.md](../../AGENTS.md), section 2). Its values become code in one place, `kernel/display/tokens.h` (stage 5 of [plans/1.2.md](../plans/1.2.md)), which a static check keeps equal to the tables below.

## 1. The reference mockup

| Item | Value |
|---|---|
| File | `Designer (2).png`, kept locally by the owner, **not in the repository** |
| SHA-256 | `6d3c1463d195c7106c5ecc2a86c068db6308d24713c2c535c3653b2a74c1762d` |
| Size | 1536×1024 pixels |
| Origin | Designed by the owner, generated with Microsoft Designer from the owner's prompts; local design reference only, not in the repo, never shipped or copied into Madire artwork |

This guide holds only colour values sampled from the mockup and a written description of its layout. No pixel of it is copied into any asset, reference renderer, test or document. Its M, its text and its taglines are inspiration only: Madire's logo is `kernel/logo.c`'s four strokes, its wordmark the 1.1 capsule letterforms (`scripts/make-wallpaper.py`), its tagline section 7's.

**The mockup's layout, in words.** A dark navy desktop, brighter towards the upper centre, crossed by thin glowing blue curves that sweep from the lower left up to the right. In the centre, a large M in a cyan-to-deep-blue gradient, the spaced capitals "MADIRE OS" below it, and a widely spaced tagline in small capitals under that. A narrow top bar (the name at the left; volume, display, date and time at the right) and a bottom taskbar (the M button in a highlighted square, then pinned app icons). A column of desktop icons at the left (Home, Files, Web, Terminal, Settings) and Trash at the lower right. Above the taskbar at the left, an open menu panel: a header with the logo and name, rows with an icon, a label and a chevron, a divider, then Shut Down. At the upper right, an About window: a title bar with an icon, the title and minimise and close buttons; a body with the logo, the wordmark, a two-column list of labels and values, a divider and a footer line. The panels are near-black navy with a one-pixel lighter edge and slightly rounded corners; the pointer is a white arrow with a dark outline. The mockup appears to be drawn at about 125 % (its taskbar is about 52 px high, the guide's 40 dp at 125 %).

**What 1.2 takes from it** (decision 18 of the plan): the wallpaper's composition and colours, the centred logo, wordmark and tagline, the bottom taskbar with the M button (the clock at the taskbar's right end instead of a top bar), the menu panel's style for the M menu (About, Console, Restart), the About window's style, and a Console window in the same style. **Not in 1.2:** the top bar, the desktop icons, Web, Settings, Files, Shut Down, the volume and display indicators.

## 2. Colours

Each token was **sampled** from the mockup (the average of a flat area, or the brightest pixel of a glow or a line) and compared with the **provisional** value of the 1.2 scope report (taken from the 1.1 wallpaper). The **adopted** column is v0.1. Contrast ratios are approximate (WCAG relative luminance); the stage that adds `tokens.h` computes them exactly.

| Token | Provisional (1.1 wallpaper) | Sampled (mockup) | Adopted v0.1 | Use |
|---|---|---|---|---|
| `bg.deep` | `#0B1220` | `#011239` (flat area, upper left) | `#011239` | wallpaper base, splash fill |
| `bg.base` | `#0F2A4A` | `#012060` (upper centre); `#011A4C` (lower right) | `#012060` | the wallpaper's brightest area (upper centre) |
| `bg.low` | – | `#011A4C` | `#011A4C` | the wallpaper's lower area |
| `surface` | `#131D30` (proposed) | `#081123` (taskbar); `#0C182A` (menu panel) | `#0C182A` | menus, window bodies |
| `surface.bar` | – | `#081123` | `#081123` | the taskbar |
| `surface.window` | – | `#0A1325` (About window body and title bar) | `#0A1325` | window body and title bar |
| `surface.active` | `#1A2740` (proposed, raised) | `#112447` (the taskbar's M button cell) | `#112447` | the active or pressed taskbar cell, a selected menu row |
| `border` | `#2A3A55` (proposed) | `#1F2B40` (About divider); `#171F37` (menu divider); `#193050` (window edge) | `#1F2B40` | dividers, one-pixel window and panel edges |
| `accent` | `#1E90FF` | `#2987F3` (the UI's blue icons) | `#2987F3` | focus, selection, the M button's mark |
| `accent.glow` | `#22D3EE` | `#029FF8` (the curves' blue core) | `#029FF8` | the glowing curves |
| `accent.light` | – | `#64F2F9` (the logo gradient's light end) | `#64F2F9` | the logo's light end, curve highlights |
| `accent.deep` | – | `#023CA1` (the logo gradient's deep blue) | `#023CA1` | the logo's deep end |
| `glow.hot` | – | `#FDFCFD` (the brightest point of a curve) | `#FDFCFD` | the hottest point of a curve's glow only |
| `text` | `#E6EDF3` | `#FAF7FE`–`#FEFBFF` (brightest pixels of the wordmark, menu labels and window text) | `#FAF9FE` | primary text |
| `text.muted` | `#8B9BB4` | not measurable (the thin tagline and footer text: their brightest pixels are anti-aliasing peaks, `#F4F4F7`–`#FBF8FD`) | `#8B9BB4` (kept) | secondary text, the tagline |
| `danger` | `#F05252` (proposed) | – | `#F05252` | errors |
| `success` | `#22C55E` (proposed) | – | `#22C55E` | success |
| `warning` | `#F5A524` (proposed) | – | `#F5A524` | warnings |

**Contrast (approximate):** `text` on `bg.deep` about 17:1; `text.muted` on `surface` about 6.3:1; `accent` on `surface.bar` about 5.3:1. **Rule:** body text needs at least 4.5:1 against its background; large text (at least 18 dp, or 14 dp bold) at least 3:1.

**Limine's backdrop:** the splash's first fill always equals Limine's `backdrop:`. Limine's `backdrop: 0B1220` and the 1.1 boot-menu wallpaper keep their colours until stage 8 chooses: the splash starts from `#0B1220`, or the backdrop becomes `bg.deep` (a listed changed check) ([plan](../plans/1.2.md), section D3).

**No translucency in 1.2:** every surface is opaque; alpha is used only for anti-aliased edges (coverage).

## 3. Fonts

- **1.2's only interface font is Terminus** (SIL OFL 1.1, decision 8), regular and bold, one size per scale step: **8×16** at 100 %, **10×20** at 125 %, **12×24** at 150 %, **16×32** at 200 %. Bold for titles and the menu header. The converted font is never called "Terminus Font" in the interface; its licence and copyright travel with Madire (`THIRD_PARTY_ASSETS.md`, created in stage 6).
- **The wordmark "MADIRE OS" and the tagline** use Madire's own capsule letterforms (1.1), drawn as strokes at any size, widely spaced (tracking about 0.4 em for the wordmark, about 0.6 em for the tagline), as in the mockup.
- **Later:** smooth fonts (Inter or Noto through stb_truetype) replace Terminus for interface text; Terminus stays the console font.

## 4. Spacing and shapes

- **Grid:** 4 dp. **Spacing tokens:** 4, 8, 12, 16, 24, 32, 48 dp.
- **Taskbar:** 40 dp high, full width, at the bottom; the M button a 40×40 dp cell at the left end; the clock at the right end, 16 dp from the edge.
- **Menu panel:** 288 dp wide; rows 36 dp high with 16 dp side padding; a 12 dp gap between the icon and the label; dividers 1 dp in `border`.
- **Windows:** a 32 dp title bar (the icon, the title, then minimise and close at the right, each a 32×32 dp target); 16–24 dp inner padding; a 1 dp edge in `border`.
- **Corners:** 6 dp radius for windows and panels, square for the taskbar; drawn with integer quarter-circles.
- **Targets:** at least 24 dp, 32 dp preferred.

## 5. Icons

- **Madire's own icons only.** Fluent UI System Icons are inspiration only and Papirus is study only; neither is ever shipped ([references.md](../references.md)).
- **Drawn on a 24 dp grid with 2 dp strokes,** stored as stroke lists (as `kernel/logo.c` stores the M) and drawn at every scale; sizes 16, 24, 32 and 48 dp.
- **Colours:** `text` for line icons, `accent` for the active state; no multicolour icons in 1.2.
- **1.2 needs only:** the M (from `kernel/logo.c`'s strokes) and two glyphs, "console" and "about".

## 6. Layout and display scaling

- **Every size is in dp** (density-independent pixels) and every element is anchored (left, right, top, bottom or centre) to the screen or to its parent; nothing is placed for one screen size.
- **Scale `s`** in quarter steps, 4, 5, 6 or 8 (100, 125, 150 or 200 %), chosen from the screen height `H`: `H < 900` → 4; `H < 1080` → 5; `H < 1440` → 6; otherwise 8.
- **Pixels from dp:** `px = (dp × s + 2) / 4`, integers only (round half up).
- **Bitmaps** scale nearest-neighbour: `x_src = x_dst × w_src / w_dst`. **Anti-aliasing** by coverage in 24.8 fixed point with an integer square root. **No floating point** anywhere: the kernel is built without SSE.
- **Smallest supported screen:** 800×600 (decision 2: any mode from 800×600 at 32 or 24 bpp).
- **The desktop's composition:** the wallpaper fills the screen; the logo, the wordmark and the tagline are centred horizontally, their block's centre at about 45 % of the height above the taskbar; the taskbar along the bottom; the M menu opens above the M button, aligned to the left edge; windows open centred, and never under the taskbar.

## 7. Tagline

**"SIMPLE · SECURE · FURTHER TOGETHER"** (decision 15), in small capitals with the middle dot `·` between the words, in `text.muted`, below the wordmark. The mockup's taglines are not used.
