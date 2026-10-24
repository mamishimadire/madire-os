#ifndef MADIRE_DISPLAY_TOKENS_H
#define MADIRE_DISPLAY_TOKENS_H

/*
 * The Madire Design Guide's colour tokens (docs/design/design-guide.md,
 * section 2: the adopted column of both colour tables, v0.1 and v0.2), as
 * 0xRRGGBB (1.3 stage 2). One define per token: `a.b.c` is UI_A_B_C. Part
 * static checks that this file equals the guide's tables exactly, and that
 * every colour constant of wallpaper_core.h and pointer_core.h equals its
 * token. Only the toolkit (ui_*) uses it so far.
 */

/* v0.1 */
#define UI_BG_DEEP          0x011239u
#define UI_BG_BASE          0x012060u
#define UI_BG_LOW           0x011A4Cu
#define UI_SURFACE          0x0C182Au
#define UI_SURFACE_BAR      0x081123u
#define UI_SURFACE_WINDOW   0x0A1325u
#define UI_SURFACE_ACTIVE   0x112447u
#define UI_BORDER           0x1F2B40u
#define UI_ACCENT           0x2987F3u
#define UI_ACCENT_GLOW      0x029FF8u
#define UI_ACCENT_LIGHT     0x64F2F9u
#define UI_ACCENT_DEEP      0x023CA1u
#define UI_GLOW_HOT         0xFDFCFDu
#define UI_TEXT             0xFAF9FEu
#define UI_TEXT_MUTED       0x8B9BB4u
#define UI_DANGER           0xF05252u
#define UI_SUCCESS          0x22C55Eu
#define UI_WARNING          0xF5A524u

/* v0.2 */
#define UI_BORDER_WINDOW    0x193050u
#define UI_BORDER_INACTIVE  0x171F37u
#define UI_BORDER_PANEL     0x182336u
#define UI_SURFACE_BAR_EDGE 0x14213Fu
#define UI_DANGER_PRESSED   0xC04141u
#define UI_TEXT_ON_ACCENT   0x011239u

#endif
