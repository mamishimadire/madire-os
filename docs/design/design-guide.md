# Madire Design Guide v0.2

**Versions:** v0.1 (Madire OS 1.2, stage 1): colours, fonts, spacing and shapes, icons, layout and display scaling, the tagline. v0.2 (Madire OS 1.3, stage 0): the desktop: the window chrome (active and inactive, the close button's states), menus (the M menu and the right-click menu), the confirmation dialog, the taskbar and its clock, the desktop's hint line, the Calculator's layout, the icon list for 1.3, six new colour tokens (four sampled from the mockup on 2026-10-24, two derived), and the contrast table (section 12: every text and icon colour against each background it appears on, computed; the stage 4 checks follow it). The contrast table changed four draft choices before adoption: the M inside controls stays in `accent` (as v0.1), a selected menu row and the open M button carry a 2 dp `accent` bar, a pressed Calculator key fills with `accent`, and no text may sit on a curve's core. Written clean-room: from the owner's mockup (below) and Madire's own 1.1 and 1.2 artwork, with published human interface guidelines read for ideas only ([references.md](../references.md)). **Amended in 1.3 stage 3b (the owner):** the desktop shows the M logo alone, at the splash's place, with no wordmark, tagline or hint line (section 6), and section 12's wallpaper text rows apply to the splash only. **Amended in 1.3 stage 4a (the owner's answers of 2026-10-29):** the About window's footer (the tagline) in the console font (section 3), its Mode value "Live (memory only)", values cut with "..." to their column, its M, wordmark and footer centred (section 8); the window drag's clamp, and a click on the empty desktop leaving no window focused (sections 6 and 8). No colour pair was added: section 12 is unchanged.

This guide sets how Madire OS looks. Every graphical part of Madire follows it; a change to it is a design change, raised before it is implemented ([AGENTS.md](../../AGENTS.md), section 2). Its values become code in one place, `kernel/display/tokens.h` (added in 1.3 stage 2; [plans/1.2.md](../plans/1.2.md) had planned it for its stage 5, where it was not made), which a static check keeps equal to the tables below; the same check keeps the colour constants of `wallpaper_core.h` and `pointer_core.h`, which predate it, equal to their tokens until 1.3 stage 3 moves them onto it.

## 1. The reference mockup

| Item | Value |
|---|---|
| File | `Designer (2).png`, kept locally by the owner, **not in the repository** |
| SHA-256 | `6d3c1463d195c7106c5ecc2a86c068db6308d24713c2c535c3653b2a74c1762d` |
| Size | 1536×1024 pixels |
| Origin | Designed by the owner, generated with Microsoft Designer from the owner's prompts; local design reference only, not in the repo, never shipped or copied into Madire artwork |

This guide holds only colour values sampled from the mockup and a written description of its layout. No pixel of it is copied into any asset, reference renderer, test or document. Its M, its text and its taglines are inspiration only: Madire's logo is `kernel/logo.c`'s four strokes, its wordmark the 1.1 capsule letterforms (`scripts/make-wallpaper.py`), its tagline section 7's.

**The mockup's layout, in words.** A dark navy desktop, brighter towards the upper centre, crossed by thin glowing blue curves that sweep from the lower left up to the right. In the centre, a large M in a cyan-to-deep-blue gradient, the spaced capitals "MADIRE OS" below it, and a widely spaced tagline in small capitals under that. A narrow top bar (the name at the left; volume, display, date and time at the right) and a bottom taskbar (the M button in a highlighted square, then pinned app icons). A column of desktop icons at the left (Home, Files, Web, Terminal, Settings) and Trash at the lower right. Above the taskbar at the left, an open menu panel: a header with the logo and name, rows with an icon, a label and a chevron, a divider, then Shut Down. At the upper right, an About window: a title bar with an icon, the title and minimise and close buttons; a body with the logo, the wordmark, a two-column list of labels and values, a divider and a footer line. The panels are near-black navy with a one-pixel lighter edge and slightly rounded corners; the pointer is a white arrow with a dark outline. The mockup appears to be drawn at about 125 % (its taskbar is about 52 px high, the guide's 40 dp at 125 %).

**What 1.2 took from it** (decision 18 of the 1.2 plan): the wallpaper's composition and colours, the centred logo, wordmark and tagline, and the splash. **What 1.3 takes** ([plans/1.3.md](../plans/1.3.md)): the bottom taskbar with the M button (the clock at the taskbar's right end instead of a top bar), the menu panel's style for the M menu (About, Console, Restart) and the right-click menu, the About window's style, a Console window and a Calculator window in the same style. **Not in 1.3:** the top bar, the desktop icons, the pinned app icons, the minimise button, Web, Settings, Files, Shut Down, the volume and display indicators.

## 2. Colours

Each token was **sampled** from the mockup (the average of a flat area, or the brightest pixel of a glow or a line) and compared with the **provisional** value of the 1.2 scope report (taken from the 1.1 wallpaper). The **adopted** column is the value in force. Contrast ratios are computed in section 12.

| Token | Provisional (1.1 wallpaper) | Sampled (mockup) | Adopted | Use |
|---|---|---|---|---|
| `bg.deep` | `#0B1220` | `#011239` (flat area, upper left) | `#011239` | wallpaper base, splash fill |
| `bg.base` | `#0F2A4A` | `#012060` (upper centre); `#011A4C` (lower right) | `#012060` | the wallpaper's brightest area (upper centre) |
| `bg.low` | – | `#011A4C` | `#011A4C` | the wallpaper's lower area |
| `surface` | `#131D30` (proposed) | `#081123` (taskbar); `#0C182A` (menu panel) | `#0C182A` | menus, window bodies |
| `surface.bar` | – | `#081123` | `#081123` | the taskbar |
| `surface.window` | – | `#0A1325` (About window body and title bar) | `#0A1325` | window body and title bar |
| `surface.active` | `#1A2740` (proposed, raised) | `#112447` (the taskbar's M button cell) | `#112447` | the active or pressed taskbar cell, a selected menu row |
| `border` | `#2A3A55` (proposed) | `#1F2B40` (About divider); `#171F37` (menu divider); `#193050` (window edge) | `#1F2B40` | dividers |
| `accent` | `#1E90FF` | `#2987F3` (the UI's blue icons) | `#2987F3` | focus, selection and its bars, the M inside controls, the primary button |
| `accent.glow` | `#22D3EE` | `#029FF8` (the curves' blue core) | `#029FF8` | the glowing curves |
| `accent.light` | – | `#64F2F9` (the logo gradient's light end) | `#64F2F9` | the logo's light end, curve highlights |
| `accent.deep` | – | `#023CA1` (the logo gradient's deep blue) | `#023CA1` | the logo's deep end |
| `glow.hot` | – | `#FDFCFD` (the brightest point of a curve) | `#FDFCFD` | the hottest point of a curve's glow only |
| `text` | `#E6EDF3` | `#FAF7FE`–`#FEFBFF` (brightest pixels of the wordmark, menu labels and window text) | `#FAF9FE` | primary text, line icons |
| `text.muted` | `#8B9BB4` | not measurable (the thin tagline and footer text: their brightest pixels are anti-aliasing peaks, `#F4F4F7`–`#FBF8FD`) | `#8B9BB4` (kept) | secondary text, the tagline, an inactive window's title |
| `danger` | `#F05252` (proposed) | – | `#F05252` | errors; the close button's hover fill |
| `success` | `#22C55E` (proposed) | – | `#22C55E` | success |
| `warning` | `#F5A524` (proposed) | – | `#F5A524` | warnings |

**New in v0.2** (sampled on 2026-10-24 from the same file, its SHA-256 checked first; single pixels across each one-pixel line, read with a standard-library PNG reader outside the repository; only the numbers are kept):

| Token | Sampled (mockup) | Adopted v0.2 | Use |
|---|---|---|---|
| `border.window` | the About window's edge: `#193050` (right edge, x 1507), `#18345C` (left edge, x 1083) | `#193050` | the one-pixel edge of the **active** window (the value v0.1 already recorded as "window edge") |
| `border.inactive` | – (the mockup shows one window only, so no inactive edge can be sampled) | `#171F37` | the one-pixel edge of an **inactive** window: v0.1's sampled menu divider, darker and greyer than `border.window`. **Derived, not sampled from an inactive window** |
| `border.panel` | the menu panel's edge: `#182336` (left edge, x 11) | `#182336` | the one-pixel edge of a menu panel |
| `surface.bar.edge` | the taskbar's top line: `#14213F` (y 971) | `#14213F` | the one-pixel line along the taskbar's top |
| `danger.pressed` | – | `#C04141` | the close button while pressed: `danger` with each channel × 0.8 (rounded down). **Derived** |
| `text.on.accent` | – | `#011239` (= `bg.deep`) | the primary button's label on `accent` |

Other numbers read on 2026-10-24, for the record: the About title bar averages `#0B1426` and its body `#0B1527` (v0.1's `surface.window` `#0A1325` kept); the About divider `#1F2B40` (= `border`); the menu header's divider `#1A273B` and the divider above Shut Down `#151F34` (v0.1's menu divider `#171F37` lies between them; dividers keep `border`); the close and minimise glyphs, the chevrons and the About window's labels peak at `#F7F3FB`–`#FCFAFE` (= `text`); the M button's cell `#132654`/`#102349` (v0.1's `surface.active` `#112447` kept). The mockup is static, so **no hover or pressed state can be sampled**: every hover and pressed colour below is chosen, and says so.

**Contrast:** computed for every pair in use in section 12. **Rule:** text needs at least 4.5:1 against its background; large text (at least 18 dp, or 14 dp bold) at least 3:1; icons and other non-text interface parts (a control's identifying graphic, a focus ring, a selection mark) at least 3:1.

**The hand-off and Limine's backdrop:** after Limine's mode set the screen is black (`000000`); the splash starts from it and cuts to its first frame ([plans/1.2.md](../plans/1.2.md), section D3, reworded in stage 8). Limine's `backdrop: 0B1220` (behind a shown menu) and the 1.1 boot-menu wallpaper keep their colours.

**No translucency:** every surface is opaque, and nothing casts a shadow; alpha is used only for anti-aliased edges (coverage).

## 3. Fonts

- **The only interface font is Terminus** (SIL OFL 1.1, decision 8 of 1.2; kept in 1.3 by decision 18), regular and bold, one size per scale step: **8×16** at 100 %, **10×20** at 125 %, **12×24** at 150 %, **16×32** at 200 %. Bold for window titles, the menu header and the primary button. The converted font is never called "Terminus Font" in the interface; its licence and copyright travel with Madire (`THIRD_PARTY_ASSETS.md`). It holds code page 437 only, so symbols outside it (the Calculator's × and the like) are stroke icons (section 5).
- **The wordmark "MADIRE OS" and the tagline** use Madire's own capsule letterforms (1.1), drawn as strokes at any size, widely spaced (tracking about 0.4 em for the wordmark, about 0.6 em for the tagline), as in the mockup. The M menu's header uses the same wordmark letterforms at a 10 dp cap height. **One exception (1.3 stage 4a, the owner):** the About window's footer, the tagline, is in the console font (regular), its middle dots code page 437's `0xFA` (section 8).
- **Later:** smooth fonts (Inter or Noto through stb_truetype) replace Terminus for interface text; Terminus stays the console font.

## 4. Spacing and shapes

- **Grid:** 4 dp. **Spacing tokens:** 4, 8, 12, 16, 24, 32, 48 dp.
- **Taskbar:** 40 dp high, full width, at the bottom; the M button a 40×40 dp cell at the left end; the clock at the right end, 16 dp from the edge.
- **Menu panel:** 288 dp wide; rows 36 dp high with 16 dp side padding; a 12 dp gap between the icon and the label; dividers 1 dp in `border`.
- **Windows:** a 32 dp title bar (the icon, the title, then the close button at the right, a 32×32 dp target); 16–24 dp inner padding; a 1 dp edge (section 8).
- **Corners:** 6 dp radius for windows, panels and dialogs, square for the taskbar; 4 dp for buttons; drawn with integer quarter-circles.
- **Targets:** at least 24 dp, 32 dp preferred.

## 5. Icons

- **Madire's own icons only.** Fluent UI System Icons are inspiration only and Papirus is study only; neither is ever shipped ([references.md](../references.md)).
- **Drawn on a 24 dp grid with 2 dp strokes and round caps,** stored as stroke lists (as `kernel/logo.c` stores the M) and drawn by coverage at every scale; sizes 16, 24, 32 and 48 dp.
- **Colours:** `text` for line icons, `accent` for the active state; no multicolour icons. The M in the logo's gradient is the logo, drawn only where it is not a control (the splash, the desktop, the About window); inside a control it is a single-colour icon (section 12: `accent.deep`, the gradient's deep end, is below 3:1 on every surface).
- **1.3 needs:**

| Icon | Drawn as | Where |
|---|---|---|
| M | `kernel/logo.c`'s strokes (the copy in the generated splash data): in `accent` in the M button (24 dp) and the M menu's header (24 dp), the logo's 210-unit width spanning 22 of the 24 dp; in the logo's gradient (`accent.deep` to `accent.light`, as the splash) in the About window (a 64 dp box, the width spanning 56 of the 64 dp; 1.3 stage 4a) | the M button, the M menu's header, the About window |
| console | a rounded rectangle with `>` and `_` inside | the Console window's title bar, the menus' Console rows |
| about | the letter i in a circle | the About window's title bar, the menus' About rows |
| restart | a circle open at the top right, with an arrowhead | the M menu's Restart row, the Restart dialog's title bar |
| calculator | a rounded rectangle with a display bar and a 2×2 key grid | the Calculator's title bar and its menu rows (if the Calculator goes ahead) |
| close | × (two strokes) | every window's close button |
| plus, minus, times, divide, backspace | the symbol's strokes | the Calculator's keys (code page 437 has no ×) |
| chevron | › | reserved for submenus; none in 1.3 |

## 6. Layout and display scaling

- **Every size is in dp** (density-independent pixels) and every element is anchored (left, right, top, bottom or centre) to the screen or to its parent; nothing is placed for one screen size.
- **Scale `s`** in quarter steps, 4, 5, 6 or 8 (100, 125, 150 or 200 %), chosen from the screen height `H`: `H < 900` → 4; `H < 1080` → 5; `H < 1440` → 6; otherwise 8.
- **Pixels from dp:** `px = (dp × s + 2) / 4`, integers only (round half up).
- **Bitmaps** scale nearest-neighbour: `x_src = x_dst × w_src / w_dst`. **Anti-aliasing** by coverage in 24.8 fixed point with an integer square root. **No floating point** anywhere: the kernel is built without SSE.
- **Smallest supported screen:** 800×600 (decision 2 of 1.2: any mode from 800×600 at 32 or 24 bpp).
- **The desktop's composition (changed by the owner in 1.3 stage 3b):** the wallpaper fills the screen with the M logo alone on it, at exactly the splash's place and size (the splash's layout, its gradient), so at the cut from the splash to the desktop the M stays still and only the splash's text disappears; no wordmark, tagline or hint on the desktop, so no text lies on the desktop's wallpaper; the taskbar along the bottom; the M menu opens above the M button (section 9); windows open centred in the area above the taskbar, each later window 24 dp lower and further right than the one below it when that place is taken, and never under the taskbar. **A dragged window (1.3 stage 4a, the owner)** moves 1:1 with the pointer, then is clamped: never under the taskbar (its bottom at most the taskbar's top), its title bar always on the screen (its top at least 0), and at least 48 dp of it on the screen at the left and the right.
- **No hint line (1.3 stage 3b, replacing decision 7's hint):** the desktop shows no "Press any key to open the console" line; any key still opens the console (the key is not typed).

## 7. Tagline

**"SIMPLE · SECURE · FURTHER TOGETHER"** (decision 15 of 1.2), in small capitals with the middle dot `·` between the words, in `text.muted`, below the wordmark. The mockup's taglines are not used.

## 8. Windows (v0.2)

- **The frame:** the title bar and the body both in `surface.window` (no line between them, as in the mockup); a 1 dp edge all round, `border.window` on the active window and `border.inactive` on the others; 6 dp corners.
- **The title bar (32 dp):** the window's icon (16 dp) 8 dp from the left edge, centred vertically; the title 8 dp after it, in bold, `text` on the active window and `text.muted` on the others; at the right end the close button, a 32×32 dp target. No minimise or maximise button in 1.3 (decision 10).
- **The close button:** the close icon (16 dp) in `text`, centred in its target. **Hover:** the target filled with `danger`, the icon in `text`. **Pressed:** filled with `danger.pressed`. The fill covers the whole 32×32 dp target; at the window's top right corner it follows the 6 dp corner. *Chosen, not sampled* (the mockup is static).
- **Focus:** the active window is the one last clicked or opened; only it receives keys. The edge and the title's colour are the only marks of focus. A click on the empty desktop leaves no window focused (all inactive; a key then opens the console); closing the focused window passes the focus to the top window left (1.3 stage 4a, the owner).
- **The Console window:** its client area is exactly the 80×25 grid at the screen's scale (640×400 at 100 %, 800×500 at 125 %, 960×600 at 150 %, 1280×800 at 200 %), with no padding; the title "Console".
- **The Console window's bottom corners (1.3 stage 4b, the owner):** the grid is clipped to the window's inner rounded shape (the window shrunk by its 1 dp edge, radius 6 dp less the edge), so the edge stays whole at the rounded corners.
- **The About window:** 352 dp wide; 24 dp padding; the M (64 dp), 16 dp, the wordmark (cap height 16 dp), 24 dp, a two-column list in the console's font (labels in `text.muted`, values in `text`, 12 dp between rows, the values' column 112 dp from the labels'): Version, Boot loader, Display, Memory, Mode (the value "Live (memory only)"); a 1 dp divider in `border` with 16 dp above and below; the tagline as the footer. No value that changes on its own (no uptime), so the window's pixels are fixed for a given boot. When it has the focus, Esc closes it (the keyboard's way to close it, also on a boot without a mouse). **Its details (1.3 stage 4a, the owner's answers):** the title "About Madire OS" with the about icon; 377 dp high (the 32 dp title bar, 24 dp, the M 64, 16, the wordmark 16, 24, five rows of 16 with 12 between, 16, the divider 1, 16, the footer 16, 24); the M, the wordmark (the splash's stroke weights and tracking at its cap height) and the footer (in the console font, `text.muted`) centred, the list at the 24 dp padding; the values: Version (the kernel's), Boot loader (the loader's own name, "-" if it gives none), Display (`WxHxD`), Memory (the usable MB, "127 MB"), Mode. **A value that does not fit its column** (the 24 dp right padding is never crossed) is cut and ends with "..." (three ASCII dots); the full value is logged.
- **The Calculator window (if it goes ahead, decision 15):** 252 dp wide (16 + 4 × 52 + 3 × 4 + 16); 16 dp padding; the display line 48 dp high, right-aligned, bold, in `text` on `surface`, 4 dp corners; then a 4×5 grid of keys, each 52×40 dp with 4 dp gaps: C, ±, %, ÷ / 7, 8, 9, × / 4, 5, 6, − / 1, 2, 3, + / 0, ., ⌫, =. Keys in `surface.active` with `text` labels; the `=` key in `accent` with `text.on.accent`; a pressed key filled with `accent`, its label or icon in `text.on.accent` (section 12: `border` on `surface.active` would be 1.08:1, too faint for a state); the operators and ⌫ are stroke icons.

## 9. Menus and dialogs (v0.2)

- **The panel:** `surface` with a 1 dp `border.panel` edge and 6 dp corners; rows 36 dp high; the row's icon (16 dp) 16 dp from the left, the label 12 dp after it in `text`; a **hovered or keyboard-selected row** filled with `surface.active` and marked by a 2 dp `accent` bar along its left edge, inset 4 dp from the row's top and bottom (*chosen*: the mockup shows none; the bar is what meets the 3:1 rule, section 12, since `surface.active` on `surface` is only 1.16:1); dividers 1 dp in `border`, 4 dp above and below; 4 dp of padding at the panel's top and bottom.
- **The M menu:** 288 dp wide; a 48 dp header with the M (24 dp) 16 dp from the left and the wordmark "MADIRE OS" (10 dp cap height) 12 dp after it, a divider under it; then the rows About, Console, a divider, Restart. It opens with the mouse with no row selected, and with the Windows key with its first row (About) selected and marked by the focus ring; the Windows key or Esc closes it, Up, Down and Enter work as in every menu. It opens with its left edge 8 dp from the screen's left edge and its bottom 8 dp above the taskbar; the M button's cell is filled with `surface.active`, with a 2 dp `accent` bar along its bottom edge, while the menu is open or the button is pressed (section 10).
- **The right-click menu (desktop):** 224 dp wide, no header; the rows "Open Console" and "About Madire OS". It opens with its top-left corner at the pointer's hot spot, moved left or up as needed to stay on the screen and above the taskbar.
- **Closing:** a click outside the menu, Esc, or choosing a row closes it; Up and Down move the selection, Enter chooses.
- **The confirmation dialog (Restart, decision 9):** a window of the section 8 style, 360 dp wide, titled "Restart", with no close button other than the title bar's; 24 dp padding; the text "Restart Madire OS now? Files in Live mode are lost." in `text`; 24 dp below it, right-aligned with 8 dp between them, two buttons 96×32 dp, 4 dp corners: **Cancel** (secondary: `surface.active`, label `text`) and **Restart** (primary: `accent`, label `text.on.accent`, bold). Cancel has the focus when the dialog opens; Enter presses the focused button, Esc and the close button cancel, Tab moves the focus. While the dialog is open it keeps the keys and clicks (other windows don't react).
- **The focus ring:** a 2 dp ring in `accent`, 2 dp outside the focused button or key, drawn only after the keyboard has moved the focus (or a menu was opened with the Windows key); in a menu the keyboard-selected row's ring lies 2 dp inside the panel's edge.

## 10. The taskbar and its clock (v0.2)

- **The bar:** 40 dp, full width, `surface.bar`, with a 1 dp `surface.bar.edge` line along its top; square corners.
- **The M button:** the 40×40 dp cell at the left end; the M (24 dp) centred in it, in `accent` (section 5); the cell filled with `surface.active` on hover, while pressed and while the M menu is open, with a 2 dp `accent` bar along the cell's bottom edge while pressed or open (*chosen*: the mockup shows only the open state; the bar meets the 3:1 rule, since `surface.active` on `surface.bar` is only 1.23:1).
- **The clock (decision 11):** 24-hour `HH:MM` (two digits each, leading zeros, a colon), in the console's font, in `text`, centred vertically, its right edge 16 dp from the screen's right edge. Read from the CMOS real-time clock, updated when the minute changes; no date and no time-zone label (QEMU's clock is UTC, VirtualBox's the host's local time). An unreadable clock shows `--:--`.
- **Nothing else in 1.3:** no pinned apps, no window buttons, no tray.

## 11. The pointer (v0.2)

- The 1.2 pointer (a 12×18 dp arrow, white with a dark outline, `kernel/display/pointer_core.c`) is the only pointer shape in 1.3; on the desktop it is always drawn, from the first mouse packet on (decision 8). No other shapes (no move or text cursors) in 1.3.

## 12. Contrast (v0.2)

Every text and icon colour against each background it appears on. Ratios are WCAG 2.x contrast ratios (relative luminance from the sRGB channels), computed on 2026-10-24 from the adopted token values with a standard-library script outside the repository, rounded to two decimals. **Rules:** **T** text, at least 4.5:1; **L** large text (at least 18 dp, or 14 dp bold), at least 3:1; **N** icons and non-text interface parts (a control's identifying graphic, a focus ring, a selection or state mark), at least 3:1. The stage 4 checks (and every later stage that draws one of these pairs) follow this table; a pair not in it is added here, with its ratio, before it is drawn.

**Text**

| Foreground | Background | Where | Ratio | Rule | Result |
|---|---|---|---|---|---|
| `text` | `surface.window` | window titles (active), About values, dialog text | 17.70:1 | T | pass |
| `text` | `surface` | menu labels, the M menu's wordmark, the Calculator's display | 16.99:1 | T | pass |
| `text` | `surface.bar` | the clock | 17.98:1 | T | pass |
| `text` | `surface.active` | a selected menu row's label, the Cancel button, Calculator key labels | 14.66:1 | T | pass |
| `text.muted` | `surface.window` | inactive window titles, About labels, the About footer (the tagline) | 6.57:1 | T | pass |
| `text.muted` | `surface` | (reserved: secondary menu text; none in 1.3) | 6.31:1 | T | pass |
| `text.on.accent` | `accent` | the Restart button's label, the `=` key, a pressed Calculator key | 5.10:1 | T | pass |
| `text` | `bg.deep` / `bg.low` / `bg.base` | the splash's wordmark on the wallpaper (its darkest to brightest flat areas; the desktop has no text on the wallpaper since 1.3 stage 3b) | 17.48 / 15.99 / 14.56:1 | L | pass |
| `text.muted` | `bg.deep` / `bg.low` / `bg.base` | the splash's text line and ready line on the wallpaper | 6.49 / 5.94 / 5.41:1 | T | pass |
| `text` | `accent.glow` | the wordmark over a curve's blue core | 2.74:1 | L | **fail: not allowed** |
| `text.muted` | `accent.glow` | the tagline or hint over a curve's core | 1.02:1 | T | **fail: not allowed** |
| `text` | `accent` | (not used: below T) | 3.42:1 | T | not used |

**Text on the wallpaper:** the wallpaper is not flat, so its rows above give only the flat areas. **No text may lie over a curve's core or its glow** (`accent.glow`, `accent.light`, `glow.hot`). Since 1.3 stage 3b the wallpaper rows above apply to the splash only: the desktop puts no text on the wallpaper (section 6), so its text-box check has nothing to check. The 1.2 splash's text does not meet these rules over the rendered wallpaper (measured in 1.3 stage 3b: the wordmark about 2.11:1, the ready line about 1.01:1, "Booting Madire OS" 4.32–5.21:1); this is a recorded follow-up, not changed in 1.3. A new text box on the wallpaper is checked against the rendered wallpaper under it before it is drawn; if it fails, the layout is raised before it is changed (the colours are not).

**Icons and non-text parts**

| Foreground | Background | Where | Ratio | Rule | Result |
|---|---|---|---|---|---|
| `text` | `surface.window` | title-bar icons, the close icon | 17.70:1 | N | pass |
| `text` | `surface` | menu row icons | 16.99:1 | N | pass |
| `text` | `surface.active` | a selected row's icon, Calculator operator icons | 14.66:1 | N | pass |
| `text` | `danger` | the close icon on hover | 3.32:1 | N | pass |
| `text` | `danger.pressed` | the close icon while pressed | 4.92:1 | N | pass |
| `text.on.accent` | `accent` | a pressed Calculator key's operator icon | 5.10:1 | N | pass |
| `accent` | `surface.bar` | the M in the M button | 5.25:1 | N | pass |
| `accent` | `surface.active` | the M in the M button while hovered, pressed or open; the selection bars on a selected row and on the open M button | 4.28:1 | N | pass |
| `accent` | `surface` | the M in the M menu's header; the menu focus ring; the selection bar beside a row | 4.96:1 | N | pass |
| `accent` | `surface.window` | the focus ring on dialog buttons and Calculator keys | 5.17:1 | N | pass |
| `danger` | `surface.window` | the close button's hover fill (a state) | 5.33:1 | N | pass |
| `danger.pressed` | `surface.window` | the close button's pressed fill (a state) | 3.60:1 | N | pass |
| `accent.deep` | `surface` / `surface.window` / `surface.bar` / `surface.active` | (the M's gradient deep end) | 1.83 / 1.91 / 1.94 / 1.58:1 | N | **fail**: so the M inside a control is `accent`, not the gradient (section 5) |
| `accent.light` | the same four surfaces | (the gradient's light end) | 13.23 / 13.78 / 14.00 / 11.42:1 | – | for the record |
| `surface.active` | `surface` / `surface.bar` / `surface.window` | the selected-row and M-cell fills alone | 1.16 / 1.23 / 1.21:1 | N | **fail alone**: each such state also carries its `accent` bar (sections 9, 10), which passes |
| `border` | `surface.active` | (a pressed key one step lighter: the draft) | 1.08:1 | N | **fail**: a pressed key fills with `accent` instead (section 8) |

**Not under a rule:** the logo in its gradient where it is not a control (the splash, the desktop, the About window: a logo, identified by the wordmark beside it); the one-pixel edges of windows, panels and the taskbar's top line, and the surfaces against the wallpaper (`surface.window`, `surface` and `surface.bar` on the wallpaper 1.12–1.22:1, `border.window` on `bg.base` 1.15:1): these edges are decorative, and each window, menu and the taskbar is identified by its title text, labels and controls, which pass above; dividers (decorative). **The active window** is told from the others by its title's colour (`text` against `text.muted`, both passing on `surface.window`) and by being on top. **The console's grid** keeps 1.2's VGA palette (its own colours, unchanged) and is outside this table.
