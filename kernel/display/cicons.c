/*
 * Madire's colour icons (1.5 stage 12; docs/plans/1.5.md, note (12)(a) and
 * the S12 change; the Design Guide, section 5): the shape tables drawn by
 * ui_cicon.c (ui_cicon.h's rules). Original work, drawn in-house. Points in
 * 1/8 dp on each design's grid (16 dp: 0-128; 48 dp: 0-384); in dp:
 *   folder      back: a rounded rectangle (4, 12)-(44, 40) r 2 with the tab
 *               (28, 13) (31, 8) (42, 8) (44, 10) (44, 13) at the right end,
 *               its left side at the M's stroke angle, `accent.hover`;
 *               front (4, 15)-(44, 40) r 2 `accent`; a 1 dp `accent.light`
 *               line (6, 15)-(42, 16) along its top (16 dp: the same at
 *               (1.5, 4)-(14.5, 13.5), the front from 5.5, the line 5.5-6.5)
 *   home folders  the folder and an emblem in `text.on.accent` centred on
 *               the front: 48 dp 2 dp strokes (a house, a monitor, a page
 *               with two lines, an arrow onto a line, a frame with a hill
 *               and a sun, a note, a play triangle); 16 dp filled shapes
 *   file        a `text` page (10, 4) (30, 4) (38, 12) (38, 44) (10, 44)
 *               with a `text.muted` folded corner (30, 4) (38, 12) (30, 12);
 *               the text file adds three `accent` lines (16 dp: the page
 *               (3, 1.5)-(13, 14.5), the corner 3 dp)
 *   link        the file and the badge: a `surface` disc (2, 30)-(18, 46)
 *               with a `text` arrow (6.5, 41.5)-(13, 35) and its head
 *               (9, 35) (13, 35) (13, 39) (16 dp: the disc (0, 9)-(7, 16),
 *               a filled arrow)
 *   trash       a tapered `text.muted` bin (12, 15) (36, 15) (33.5, 44)
 *               (14.5, 44) with three `bg.deep` slots, a `text` lid
 *               (9, 10)-(39, 14) and handle (19, 6.5)-(29, 10); full: no
 *               handle, an `accent.light` and a `warning` sheet above the lid
 *   1.5 stage 14b (This PC): thispc a `text` monitor (5, 8)-(43, 34) r 2
 *               with an `accent` screen (8, 11)-(40, 31), a `text.muted`
 *               stand and a `text` foot; disk a `text` drive (5, 15)-(43, 35)
 *               r 3 with a `bg.deep` slot and an `accent` light; memory an
 *               `accent` module (4, 14)-(44, 30) with three `bg.deep` chips
 *               and seven `warning` contacts; cd a `text.muted` disc (6, 6)-
 *               (42, 42) with a `bg.deep` hole and a `text` highlight (16 dp:
 *               the same, simplified)
 * tests/desktop_ref.py holds the same designs in dp (its CICONS).
 */
#include "kernel/display/ui_cicon.h"

const int16_t cicon_points[][2] = {
    { 12, 32 }, { 116, 108 }, { 72, 36 }, { 82, 20 }, { 112, 20 }, { 116, 24 }, { 116, 36 }, { 12, 44 },
    { 116, 108 }, { 20, 44 }, { 108, 52 }, { 32, 96 }, { 352, 320 }, { 224, 104 }, { 248, 64 }, { 336, 64 },
    { 352, 80 }, { 352, 104 }, { 32, 120 }, { 352, 320 }, { 48, 120 }, { 336, 128 }, { 12, 32 }, { 116, 108 },
    { 72, 36 }, { 82, 20 }, { 112, 20 }, { 116, 24 }, { 116, 36 }, { 12, 44 }, { 116, 108 }, { 20, 44 },
    { 108, 52 }, { 64, 56 }, { 86, 76 }, { 80, 76 }, { 80, 96 }, { 48, 96 }, { 48, 76 }, { 42, 76 }, { 32, 96 },
    { 352, 320 }, { 224, 104 }, { 248, 64 }, { 336, 64 }, { 352, 80 }, { 352, 104 }, { 32, 120 }, { 352, 320 },
    { 48, 120 }, { 336, 128 }, { 136, 224 }, { 192, 168 }, { 248, 224 }, { 160, 208 }, { 160, 272 }, { 224, 272 },
    { 224, 208 }, { 12, 32 }, { 116, 108 }, { 72, 36 }, { 82, 20 }, { 112, 20 }, { 116, 24 }, { 116, 36 },
    { 12, 44 }, { 116, 108 }, { 20, 44 }, { 108, 52 }, { 42, 60 }, { 86, 86 }, { 54, 88 }, { 74, 96 }, { 32, 96 },
    { 352, 320 }, { 224, 104 }, { 248, 64 }, { 336, 64 }, { 352, 80 }, { 352, 104 }, { 32, 120 }, { 352, 320 },
    { 48, 120 }, { 336, 128 }, { 136, 176 }, { 248, 176 }, { 248, 248 }, { 136, 248 }, { 136, 176 }, { 192, 248 },
    { 192, 272 }, { 160, 272 }, { 224, 272 }, { 12, 32 }, { 116, 108 }, { 72, 36 }, { 82, 20 }, { 112, 20 },
    { 116, 24 }, { 116, 36 }, { 12, 44 }, { 116, 108 }, { 20, 44 }, { 108, 52 }, { 48, 56 }, { 72, 56 }, { 82, 66 },
    { 82, 98 }, { 48, 98 }, { 32, 96 }, { 352, 320 }, { 224, 104 }, { 248, 64 }, { 336, 64 }, { 352, 80 },
    { 352, 104 }, { 32, 120 }, { 352, 320 }, { 48, 120 }, { 336, 128 }, { 152, 168 }, { 208, 168 }, { 232, 192 },
    { 232, 280 }, { 152, 280 }, { 152, 168 }, { 172, 216 }, { 212, 216 }, { 172, 248 }, { 212, 248 }, { 12, 32 },
    { 116, 108 }, { 72, 36 }, { 82, 20 }, { 112, 20 }, { 116, 24 }, { 116, 36 }, { 12, 44 }, { 116, 108 },
    { 20, 44 }, { 108, 52 }, { 58, 56 }, { 70, 56 }, { 70, 76 }, { 84, 76 }, { 64, 96 }, { 44, 76 }, { 58, 76 },
    { 32, 96 }, { 352, 320 }, { 224, 104 }, { 248, 64 }, { 336, 64 }, { 352, 80 }, { 352, 104 }, { 32, 120 },
    { 352, 320 }, { 48, 120 }, { 336, 128 }, { 192, 168 }, { 192, 240 }, { 156, 204 }, { 192, 240 }, { 228, 204 },
    { 144, 272 }, { 240, 272 }, { 12, 32 }, { 116, 108 }, { 72, 36 }, { 82, 20 }, { 112, 20 }, { 116, 24 },
    { 116, 36 }, { 12, 44 }, { 116, 108 }, { 20, 44 }, { 108, 52 }, { 42, 98 }, { 60, 72 }, { 72, 84 }, { 78, 78 },
    { 86, 98 }, { 72, 54 }, { 84, 66 }, { 32, 96 }, { 352, 320 }, { 224, 104 }, { 248, 64 }, { 336, 64 },
    { 352, 80 }, { 352, 104 }, { 32, 120 }, { 352, 320 }, { 48, 120 }, { 336, 128 }, { 136, 176 }, { 248, 176 },
    { 248, 272 }, { 136, 272 }, { 136, 176 }, { 160, 248 }, { 192, 212 }, { 224, 248 }, { 220, 204 }, { 12, 32 },
    { 116, 108 }, { 72, 36 }, { 82, 20 }, { 112, 20 }, { 116, 24 }, { 116, 36 }, { 12, 44 }, { 116, 108 },
    { 20, 44 }, { 108, 52 }, { 68, 56 }, { 76, 92 }, { 48, 80 }, { 76, 100 }, { 76, 56 }, { 88, 62 }, { 88, 72 },
    { 76, 66 }, { 32, 96 }, { 352, 320 }, { 224, 104 }, { 248, 64 }, { 336, 64 }, { 352, 80 }, { 352, 104 },
    { 32, 120 }, { 352, 320 }, { 48, 120 }, { 336, 128 }, { 216, 168 }, { 216, 256 }, { 216, 168 }, { 248, 184 },
    { 216, 256 }, { 211, 267 }, { 200, 272 }, { 189, 267 }, { 184, 256 }, { 189, 245 }, { 200, 240 }, { 211, 245 },
    { 216, 256 }, { 12, 32 }, { 116, 108 }, { 72, 36 }, { 82, 20 }, { 112, 20 }, { 116, 24 }, { 116, 36 },
    { 12, 44 }, { 116, 108 }, { 20, 44 }, { 108, 52 }, { 52, 58 }, { 86, 78 }, { 52, 98 }, { 32, 96 }, { 352, 320 },
    { 224, 104 }, { 248, 64 }, { 336, 64 }, { 352, 80 }, { 352, 104 }, { 32, 120 }, { 352, 320 }, { 48, 120 },
    { 336, 128 }, { 168, 176 }, { 240, 224 }, { 168, 272 }, { 168, 176 }, { 24, 12 }, { 80, 12 }, { 104, 36 },
    { 104, 116 }, { 24, 116 }, { 80, 12 }, { 104, 36 }, { 80, 36 }, { 80, 32 }, { 240, 32 }, { 304, 96 },
    { 304, 352 }, { 80, 352 }, { 240, 32 }, { 304, 96 }, { 240, 96 }, { 24, 12 }, { 80, 12 }, { 104, 36 },
    { 104, 116 }, { 24, 116 }, { 80, 12 }, { 104, 36 }, { 80, 36 }, { 40, 56 }, { 88, 64 }, { 40, 76 }, { 88, 84 },
    { 40, 96 }, { 72, 104 }, { 80, 32 }, { 240, 32 }, { 304, 96 }, { 304, 352 }, { 80, 352 }, { 240, 32 },
    { 304, 96 }, { 240, 96 }, { 120, 160 }, { 264, 176 }, { 120, 208 }, { 264, 224 }, { 120, 256 }, { 216, 272 },
    { 24, 12 }, { 80, 12 }, { 104, 36 }, { 104, 116 }, { 24, 116 }, { 80, 12 }, { 104, 36 }, { 80, 36 }, { 0, 72 },
    { 56, 128 }, { 18, 86 }, { 42, 86 }, { 42, 110 }, { 12, 110 }, { 30, 92 }, { 36, 98 }, { 18, 116 }, { 80, 32 },
    { 240, 32 }, { 304, 96 }, { 304, 352 }, { 80, 352 }, { 240, 32 }, { 304, 96 }, { 240, 96 }, { 16, 240 },
    { 144, 368 }, { 52, 332 }, { 104, 280 }, { 72, 280 }, { 104, 280 }, { 104, 312 }, { 0, 72 }, { 56, 128 },
    { 18, 86 }, { 42, 86 }, { 42, 110 }, { 12, 110 }, { 30, 92 }, { 36, 98 }, { 18, 116 }, { 16, 240 },
    { 144, 368 }, { 52, 332 }, { 104, 280 }, { 72, 280 }, { 104, 280 }, { 104, 312 }, { 32, 44 }, { 96, 44 },
    { 90, 116 }, { 38, 116 }, { 46, 56 }, { 52, 104 }, { 61, 56 }, { 67, 104 }, { 76, 56 }, { 82, 104 }, { 24, 28 },
    { 104, 40 }, { 52, 16 }, { 76, 28 }, { 96, 120 }, { 288, 120 }, { 268, 352 }, { 116, 352 }, { 144, 160 },
    { 160, 312 }, { 184, 160 }, { 200, 312 }, { 224, 160 }, { 240, 312 }, { 72, 80 }, { 312, 112 }, { 152, 52 },
    { 232, 80 }, { 32, 44 }, { 96, 44 }, { 90, 116 }, { 38, 116 }, { 46, 56 }, { 52, 104 }, { 61, 56 }, { 67, 104 },
    { 76, 56 }, { 82, 104 }, { 24, 28 }, { 104, 40 }, { 28, 28 }, { 52, 8 }, { 64, 20 }, { 48, 28 }, { 64, 28 },
    { 88, 10 }, { 100, 22 }, { 92, 28 }, { 96, 120 }, { 288, 120 }, { 268, 352 }, { 116, 352 }, { 144, 160 },
    { 160, 312 }, { 184, 160 }, { 200, 312 }, { 224, 160 }, { 240, 312 }, { 72, 80 }, { 312, 112 }, { 96, 80 },
    { 160, 28 }, { 188, 60 }, { 140, 80 }, { 176, 80 }, { 248, 32 }, { 280, 68 }, { 256, 80 },
    /* 1.5 stage 14b: This PC, the disk, the memory, the CD drive */
    { 12, 20 }, { 116, 92 }, { 24, 32 }, { 104, 80 }, { 52, 92 }, { 76, 92 }, { 80, 104 }, { 48, 104 }, { 32, 104 },
    { 96, 116 }, { 40, 64 }, { 344, 272 }, { 64, 88 }, { 320, 248 }, { 160, 272 }, { 224, 272 }, { 240, 320 },
    { 144, 320 }, { 104, 320 }, { 280, 348 }, { 8, 36 }, { 120, 92 }, { 24, 56 }, { 72, 68 }, { 88, 52 },
    { 108, 72 }, { 40, 120 }, { 344, 280 }, { 80, 188 }, { 224, 212 }, { 264, 180 }, { 304, 220 }, { 8, 32 },
    { 120, 84 }, { 24, 44 }, { 48, 72 }, { 52, 44 }, { 76, 72 }, { 80, 44 }, { 104, 72 }, { 16, 84 }, { 112, 100 },
    { 32, 112 }, { 352, 240 }, { 64, 136 }, { 128, 200 }, { 160, 136 }, { 224, 200 }, { 256, 136 }, { 320, 200 },
    { 56, 240 }, { 76, 272 }, { 96, 240 }, { 116, 272 }, { 136, 240 }, { 156, 272 }, { 176, 240 }, { 196, 272 },
    { 216, 240 }, { 236, 272 }, { 256, 240 }, { 276, 272 }, { 296, 240 }, { 316, 272 }, { 12, 12 }, { 116, 116 },
    { 48, 48 }, { 80, 80 }, { 28, 64 }, { 36, 40 }, { 56, 26 }, { 59, 34 }, { 44, 46 }, { 38, 64 }, { 48, 48 },
    { 336, 336 }, { 148, 148 }, { 236, 236 }, { 96, 192 }, { 112, 128 }, { 160, 88 }, { 168, 104 }, { 128, 136 },
    { 116, 192 },
};

const struct cicon_shape cicon_shapes[] = {
    /* folder, 16 dp */
    { CICON_RRECT, CI_ACCENT_HOVER, CI_TRACE_BG, 2, 0, 8 },
    { CICON_POLY, CI_ACCENT_HOVER, CI_TRACE_BG, 5, 2, 0 },
    { CICON_RRECT, CI_ACCENT, CI_TRACE_BG, 2, 7, 8 },
    { CICON_RRECT, CI_ACCENT_LIGHT, CI_TRACE_NONE, 2, 9, 0 },
    /* folder, 48 dp */
    { CICON_RRECT, CI_ACCENT_HOVER, CI_TRACE_BG, 2, 11, 16 },
    { CICON_POLY, CI_ACCENT_HOVER, CI_TRACE_BG, 5, 13, 0 },
    { CICON_RRECT, CI_ACCENT, CI_TRACE_BG, 2, 18, 16 },
    { CICON_RRECT, CI_ACCENT_LIGHT, CI_TRACE_NONE, 2, 20, 0 },
    /* home, 16 dp */
    { CICON_RRECT, CI_ACCENT_HOVER, CI_TRACE_BG, 2, 22, 8 },
    { CICON_POLY, CI_ACCENT_HOVER, CI_TRACE_BG, 5, 24, 0 },
    { CICON_RRECT, CI_ACCENT, CI_TRACE_BG, 2, 29, 8 },
    { CICON_RRECT, CI_ACCENT_LIGHT, CI_TRACE_NONE, 2, 31, 0 },
    { CICON_POLY, CI_TEXT_ON_ACCENT, CI_ACCENT, 7, 33, 0 },
    /* home, 48 dp */
    { CICON_RRECT, CI_ACCENT_HOVER, CI_TRACE_BG, 2, 40, 16 },
    { CICON_POLY, CI_ACCENT_HOVER, CI_TRACE_BG, 5, 42, 0 },
    { CICON_RRECT, CI_ACCENT, CI_TRACE_BG, 2, 47, 16 },
    { CICON_RRECT, CI_ACCENT_LIGHT, CI_TRACE_NONE, 2, 49, 0 },
    { CICON_STROKE, CI_TEXT_ON_ACCENT, CI_ACCENT, 3, 51, 0 },
    { CICON_STROKE, CI_TEXT_ON_ACCENT, CI_ACCENT, 4, 54, 0 },
    /* desktop, 16 dp */
    { CICON_RRECT, CI_ACCENT_HOVER, CI_TRACE_BG, 2, 58, 8 },
    { CICON_POLY, CI_ACCENT_HOVER, CI_TRACE_BG, 5, 60, 0 },
    { CICON_RRECT, CI_ACCENT, CI_TRACE_BG, 2, 65, 8 },
    { CICON_RRECT, CI_ACCENT_LIGHT, CI_TRACE_NONE, 2, 67, 0 },
    { CICON_RRECT, CI_TEXT_ON_ACCENT, CI_ACCENT, 2, 69, 0 },
    { CICON_RRECT, CI_TEXT_ON_ACCENT, CI_ACCENT, 2, 71, 0 },
    /* desktop, 48 dp */
    { CICON_RRECT, CI_ACCENT_HOVER, CI_TRACE_BG, 2, 73, 16 },
    { CICON_POLY, CI_ACCENT_HOVER, CI_TRACE_BG, 5, 75, 0 },
    { CICON_RRECT, CI_ACCENT, CI_TRACE_BG, 2, 80, 16 },
    { CICON_RRECT, CI_ACCENT_LIGHT, CI_TRACE_NONE, 2, 82, 0 },
    { CICON_STROKE, CI_TEXT_ON_ACCENT, CI_ACCENT, 5, 84, 0 },
    { CICON_STROKE, CI_TEXT_ON_ACCENT, CI_ACCENT, 2, 89, 0 },
    { CICON_STROKE, CI_TEXT_ON_ACCENT, CI_ACCENT, 2, 91, 0 },
    /* documents, 16 dp */
    { CICON_RRECT, CI_ACCENT_HOVER, CI_TRACE_BG, 2, 93, 8 },
    { CICON_POLY, CI_ACCENT_HOVER, CI_TRACE_BG, 5, 95, 0 },
    { CICON_RRECT, CI_ACCENT, CI_TRACE_BG, 2, 100, 8 },
    { CICON_RRECT, CI_ACCENT_LIGHT, CI_TRACE_NONE, 2, 102, 0 },
    { CICON_POLY, CI_TEXT_ON_ACCENT, CI_ACCENT, 5, 104, 0 },
    /* documents, 48 dp */
    { CICON_RRECT, CI_ACCENT_HOVER, CI_TRACE_BG, 2, 109, 16 },
    { CICON_POLY, CI_ACCENT_HOVER, CI_TRACE_BG, 5, 111, 0 },
    { CICON_RRECT, CI_ACCENT, CI_TRACE_BG, 2, 116, 16 },
    { CICON_RRECT, CI_ACCENT_LIGHT, CI_TRACE_NONE, 2, 118, 0 },
    { CICON_STROKE, CI_TEXT_ON_ACCENT, CI_ACCENT, 6, 120, 0 },
    { CICON_STROKE, CI_TEXT_ON_ACCENT, CI_ACCENT, 2, 126, 0 },
    { CICON_STROKE, CI_TEXT_ON_ACCENT, CI_ACCENT, 2, 128, 0 },
    /* downloads, 16 dp */
    { CICON_RRECT, CI_ACCENT_HOVER, CI_TRACE_BG, 2, 130, 8 },
    { CICON_POLY, CI_ACCENT_HOVER, CI_TRACE_BG, 5, 132, 0 },
    { CICON_RRECT, CI_ACCENT, CI_TRACE_BG, 2, 137, 8 },
    { CICON_RRECT, CI_ACCENT_LIGHT, CI_TRACE_NONE, 2, 139, 0 },
    { CICON_POLY, CI_TEXT_ON_ACCENT, CI_ACCENT, 7, 141, 0 },
    /* downloads, 48 dp */
    { CICON_RRECT, CI_ACCENT_HOVER, CI_TRACE_BG, 2, 148, 16 },
    { CICON_POLY, CI_ACCENT_HOVER, CI_TRACE_BG, 5, 150, 0 },
    { CICON_RRECT, CI_ACCENT, CI_TRACE_BG, 2, 155, 16 },
    { CICON_RRECT, CI_ACCENT_LIGHT, CI_TRACE_NONE, 2, 157, 0 },
    { CICON_STROKE, CI_TEXT_ON_ACCENT, CI_ACCENT, 2, 159, 0 },
    { CICON_STROKE, CI_TEXT_ON_ACCENT, CI_ACCENT, 3, 161, 0 },
    { CICON_STROKE, CI_TEXT_ON_ACCENT, CI_ACCENT, 2, 164, 0 },
    /* pictures, 16 dp */
    { CICON_RRECT, CI_ACCENT_HOVER, CI_TRACE_BG, 2, 166, 8 },
    { CICON_POLY, CI_ACCENT_HOVER, CI_TRACE_BG, 5, 168, 0 },
    { CICON_RRECT, CI_ACCENT, CI_TRACE_BG, 2, 173, 8 },
    { CICON_RRECT, CI_ACCENT_LIGHT, CI_TRACE_NONE, 2, 175, 0 },
    { CICON_POLY, CI_TEXT_ON_ACCENT, CI_ACCENT, 5, 177, 0 },
    { CICON_RRECT, CI_TEXT_ON_ACCENT, CI_ACCENT, 2, 182, 6 },
    /* pictures, 48 dp */
    { CICON_RRECT, CI_ACCENT_HOVER, CI_TRACE_BG, 2, 184, 16 },
    { CICON_POLY, CI_ACCENT_HOVER, CI_TRACE_BG, 5, 186, 0 },
    { CICON_RRECT, CI_ACCENT, CI_TRACE_BG, 2, 191, 16 },
    { CICON_RRECT, CI_ACCENT_LIGHT, CI_TRACE_NONE, 2, 193, 0 },
    { CICON_STROKE, CI_TEXT_ON_ACCENT, CI_ACCENT, 5, 195, 0 },
    { CICON_STROKE, CI_TEXT_ON_ACCENT, CI_ACCENT, 3, 200, 0 },
    { CICON_STROKE, CI_TEXT_ON_ACCENT, CI_ACCENT, 1, 203, 0 },
    /* music, 16 dp */
    { CICON_RRECT, CI_ACCENT_HOVER, CI_TRACE_BG, 2, 204, 8 },
    { CICON_POLY, CI_ACCENT_HOVER, CI_TRACE_BG, 5, 206, 0 },
    { CICON_RRECT, CI_ACCENT, CI_TRACE_BG, 2, 211, 8 },
    { CICON_RRECT, CI_ACCENT_LIGHT, CI_TRACE_NONE, 2, 213, 0 },
    { CICON_RRECT, CI_TEXT_ON_ACCENT, CI_ACCENT, 2, 215, 0 },
    { CICON_RRECT, CI_TEXT_ON_ACCENT, CI_ACCENT, 2, 217, 10 },
    { CICON_POLY, CI_TEXT_ON_ACCENT, CI_ACCENT, 4, 219, 0 },
    /* music, 48 dp */
    { CICON_RRECT, CI_ACCENT_HOVER, CI_TRACE_BG, 2, 223, 16 },
    { CICON_POLY, CI_ACCENT_HOVER, CI_TRACE_BG, 5, 225, 0 },
    { CICON_RRECT, CI_ACCENT, CI_TRACE_BG, 2, 230, 16 },
    { CICON_RRECT, CI_ACCENT_LIGHT, CI_TRACE_NONE, 2, 232, 0 },
    { CICON_STROKE, CI_TEXT_ON_ACCENT, CI_ACCENT, 2, 234, 0 },
    { CICON_STROKE, CI_TEXT_ON_ACCENT, CI_ACCENT, 2, 236, 0 },
    { CICON_STROKE, CI_TEXT_ON_ACCENT, CI_ACCENT, 9, 238, 0 },
    /* videos, 16 dp */
    { CICON_RRECT, CI_ACCENT_HOVER, CI_TRACE_BG, 2, 247, 8 },
    { CICON_POLY, CI_ACCENT_HOVER, CI_TRACE_BG, 5, 249, 0 },
    { CICON_RRECT, CI_ACCENT, CI_TRACE_BG, 2, 254, 8 },
    { CICON_RRECT, CI_ACCENT_LIGHT, CI_TRACE_NONE, 2, 256, 0 },
    { CICON_POLY, CI_TEXT_ON_ACCENT, CI_ACCENT, 3, 258, 0 },
    /* videos, 48 dp */
    { CICON_RRECT, CI_ACCENT_HOVER, CI_TRACE_BG, 2, 261, 16 },
    { CICON_POLY, CI_ACCENT_HOVER, CI_TRACE_BG, 5, 263, 0 },
    { CICON_RRECT, CI_ACCENT, CI_TRACE_BG, 2, 268, 16 },
    { CICON_RRECT, CI_ACCENT_LIGHT, CI_TRACE_NONE, 2, 270, 0 },
    { CICON_STROKE, CI_TEXT_ON_ACCENT, CI_ACCENT, 4, 272, 0 },
    /* file, 16 dp */
    { CICON_POLY, CI_TEXT, CI_TRACE_BG, 5, 276, 0 },
    { CICON_POLY, CI_TEXT_MUTED, CI_TRACE_NONE, 3, 281, 0 },
    /* file, 48 dp */
    { CICON_POLY, CI_TEXT, CI_TRACE_BG, 5, 284, 0 },
    { CICON_POLY, CI_TEXT_MUTED, CI_TRACE_NONE, 3, 289, 0 },
    /* textfile, 16 dp */
    { CICON_POLY, CI_TEXT, CI_TRACE_BG, 5, 292, 0 },
    { CICON_POLY, CI_TEXT_MUTED, CI_TRACE_NONE, 3, 297, 0 },
    { CICON_RRECT, CI_ACCENT, CI_TEXT, 2, 300, 0 },
    { CICON_RRECT, CI_ACCENT, CI_TEXT, 2, 302, 0 },
    { CICON_RRECT, CI_ACCENT, CI_TEXT, 2, 304, 0 },
    /* textfile, 48 dp */
    { CICON_POLY, CI_TEXT, CI_TRACE_BG, 5, 306, 0 },
    { CICON_POLY, CI_TEXT_MUTED, CI_TRACE_NONE, 3, 311, 0 },
    { CICON_RRECT, CI_ACCENT, CI_TEXT, 2, 314, 0 },
    { CICON_RRECT, CI_ACCENT, CI_TEXT, 2, 316, 0 },
    { CICON_RRECT, CI_ACCENT, CI_TEXT, 2, 318, 0 },
    /* link, 16 dp */
    { CICON_POLY, CI_TEXT, CI_TRACE_BG, 5, 320, 0 },
    { CICON_POLY, CI_TEXT_MUTED, CI_TRACE_NONE, 3, 325, 0 },
    { CICON_RRECT, CI_SURFACE, CI_TRACE_NONE, 2, 328, 28 },
    { CICON_POLY, CI_TEXT, CI_SURFACE, 3, 330, 0 },
    { CICON_POLY, CI_TEXT, CI_SURFACE, 4, 333, 0 },
    /* link, 48 dp */
    { CICON_POLY, CI_TEXT, CI_TRACE_BG, 5, 337, 0 },
    { CICON_POLY, CI_TEXT_MUTED, CI_TRACE_NONE, 3, 342, 0 },
    { CICON_RRECT, CI_SURFACE, CI_TRACE_NONE, 2, 345, 64 },
    { CICON_STROKE, CI_TEXT, CI_SURFACE, 2, 347, 0 },
    { CICON_STROKE, CI_TEXT, CI_SURFACE, 3, 349, 0 },
    /* badge, 16 dp */
    { CICON_RRECT, CI_SURFACE, CI_TRACE_NONE, 2, 352, 28 },
    { CICON_POLY, CI_TEXT, CI_SURFACE, 3, 354, 0 },
    { CICON_POLY, CI_TEXT, CI_SURFACE, 4, 357, 0 },
    /* badge, 48 dp */
    { CICON_RRECT, CI_SURFACE, CI_TRACE_NONE, 2, 361, 64 },
    { CICON_STROKE, CI_TEXT, CI_SURFACE, 2, 363, 0 },
    { CICON_STROKE, CI_TEXT, CI_SURFACE, 3, 365, 0 },
    /* trash, 16 dp */
    { CICON_POLY, CI_TEXT_MUTED, CI_TRACE_BG, 4, 368, 0 },
    { CICON_RRECT, CI_BG_DEEP, CI_TRACE_NONE, 2, 372, 0 },
    { CICON_RRECT, CI_BG_DEEP, CI_TRACE_NONE, 2, 374, 0 },
    { CICON_RRECT, CI_BG_DEEP, CI_TRACE_NONE, 2, 376, 0 },
    { CICON_RRECT, CI_TEXT, CI_TRACE_BG, 2, 378, 4 },
    { CICON_RRECT, CI_TEXT, CI_TRACE_BG, 2, 380, 0 },
    /* trash, 48 dp */
    { CICON_POLY, CI_TEXT_MUTED, CI_TRACE_BG, 4, 382, 0 },
    { CICON_RRECT, CI_BG_DEEP, CI_TRACE_NONE, 2, 386, 0 },
    { CICON_RRECT, CI_BG_DEEP, CI_TRACE_NONE, 2, 388, 0 },
    { CICON_RRECT, CI_BG_DEEP, CI_TRACE_NONE, 2, 390, 0 },
    { CICON_RRECT, CI_TEXT, CI_TRACE_BG, 2, 392, 8 },
    { CICON_RRECT, CI_TEXT, CI_TRACE_BG, 2, 394, 0 },
    /* trash_full, 16 dp */
    { CICON_POLY, CI_TEXT_MUTED, CI_TRACE_BG, 4, 396, 0 },
    { CICON_RRECT, CI_BG_DEEP, CI_TRACE_NONE, 2, 400, 0 },
    { CICON_RRECT, CI_BG_DEEP, CI_TRACE_NONE, 2, 402, 0 },
    { CICON_RRECT, CI_BG_DEEP, CI_TRACE_NONE, 2, 404, 0 },
    { CICON_RRECT, CI_TEXT, CI_TRACE_BG, 2, 406, 4 },
    { CICON_POLY, CI_ACCENT_LIGHT, CI_TRACE_BG, 4, 408, 0 },
    { CICON_POLY, CI_WARNING, CI_TRACE_BG, 4, 412, 0 },
    /* trash_full, 48 dp */
    { CICON_POLY, CI_TEXT_MUTED, CI_TRACE_BG, 4, 416, 0 },
    { CICON_RRECT, CI_BG_DEEP, CI_TRACE_NONE, 2, 420, 0 },
    { CICON_RRECT, CI_BG_DEEP, CI_TRACE_NONE, 2, 422, 0 },
    { CICON_RRECT, CI_BG_DEEP, CI_TRACE_NONE, 2, 424, 0 },
    { CICON_RRECT, CI_TEXT, CI_TRACE_BG, 2, 426, 8 },
    { CICON_POLY, CI_ACCENT_LIGHT, CI_TRACE_BG, 4, 428, 0 },
    { CICON_POLY, CI_WARNING, CI_TRACE_BG, 4, 432, 0 },
    /* thispc, 16 dp */
    { CICON_RRECT, CI_TEXT, CI_TRACE_BG, 2, 436, 8 },
    { CICON_RRECT, CI_ACCENT, CI_TEXT, 2, 438, 0 },
    { CICON_POLY, CI_TEXT_MUTED, CI_TRACE_BG, 4, 440, 0 },
    { CICON_RRECT, CI_TEXT, CI_TRACE_BG, 2, 444, 4 },
    /* thispc, 48 dp */
    { CICON_RRECT, CI_TEXT, CI_TRACE_BG, 2, 446, 16 },
    { CICON_RRECT, CI_ACCENT, CI_TEXT, 2, 448, 8 },
    { CICON_POLY, CI_TEXT_MUTED, CI_TRACE_BG, 4, 450, 0 },
    { CICON_RRECT, CI_TEXT, CI_TRACE_BG, 2, 454, 12 },
    /* disk, 16 dp */
    { CICON_RRECT, CI_TEXT, CI_TRACE_BG, 2, 456, 8 },
    { CICON_RRECT, CI_BG_DEEP, CI_TEXT, 2, 458, 0 },
    { CICON_RRECT, CI_ACCENT, CI_TEXT, 2, 460, 10 },
    /* disk, 48 dp */
    { CICON_RRECT, CI_TEXT, CI_TRACE_BG, 2, 462, 24 },
    { CICON_RRECT, CI_BG_DEEP, CI_TEXT, 2, 464, 8 },
    { CICON_RRECT, CI_ACCENT, CI_TEXT, 2, 466, 20 },
    /* memory, 16 dp */
    { CICON_RRECT, CI_ACCENT, CI_TRACE_BG, 2, 468, 4 },
    { CICON_RRECT, CI_BG_DEEP, CI_ACCENT, 2, 470, 0 },
    { CICON_RRECT, CI_BG_DEEP, CI_ACCENT, 2, 472, 0 },
    { CICON_RRECT, CI_BG_DEEP, CI_ACCENT, 2, 474, 0 },
    { CICON_RRECT, CI_WARNING, CI_TRACE_BG, 2, 476, 0 },
    /* memory, 48 dp */
    { CICON_RRECT, CI_ACCENT, CI_TRACE_BG, 2, 478, 12 },
    { CICON_RRECT, CI_BG_DEEP, CI_ACCENT, 2, 480, 0 },
    { CICON_RRECT, CI_BG_DEEP, CI_ACCENT, 2, 482, 0 },
    { CICON_RRECT, CI_BG_DEEP, CI_ACCENT, 2, 484, 0 },
    { CICON_RRECT, CI_WARNING, CI_TRACE_BG, 2, 486, 0 },
    { CICON_RRECT, CI_WARNING, CI_TRACE_BG, 2, 488, 0 },
    { CICON_RRECT, CI_WARNING, CI_TRACE_BG, 2, 490, 0 },
    { CICON_RRECT, CI_WARNING, CI_TRACE_BG, 2, 492, 0 },
    { CICON_RRECT, CI_WARNING, CI_TRACE_BG, 2, 494, 0 },
    { CICON_RRECT, CI_WARNING, CI_TRACE_BG, 2, 496, 0 },
    { CICON_RRECT, CI_WARNING, CI_TRACE_BG, 2, 498, 0 },
    /* cd, 16 dp */
    { CICON_RRECT, CI_TEXT_MUTED, CI_TRACE_BG, 2, 500, 52 },
    { CICON_RRECT, CI_BG_DEEP, CI_TEXT_MUTED, 2, 502, 16 },
    { CICON_POLY, CI_TEXT, CI_TRACE_NONE, 6, 504, 0 },
    /* cd, 48 dp */
    { CICON_RRECT, CI_TEXT_MUTED, CI_TRACE_BG, 2, 510, 144 },
    { CICON_RRECT, CI_BG_DEEP, CI_TEXT_MUTED, 2, 512, 44 },
    { CICON_POLY, CI_TEXT, CI_TRACE_NONE, 6, 514, 0 },
};

const struct cicon_def cicon_defs[UI_CICON_COUNT][2] = {
    { { 0, 4 }, { 4, 4 } }, /* folder */
    { { 8, 5 }, { 13, 6 } }, /* home */
    { { 19, 6 }, { 25, 7 } }, /* desktop */
    { { 32, 5 }, { 37, 7 } }, /* documents */
    { { 44, 5 }, { 49, 7 } }, /* downloads */
    { { 56, 6 }, { 62, 7 } }, /* pictures */
    { { 69, 7 }, { 76, 7 } }, /* music */
    { { 83, 5 }, { 88, 5 } }, /* videos */
    { { 93, 2 }, { 95, 2 } }, /* file */
    { { 97, 5 }, { 102, 5 } }, /* textfile */
    { { 107, 5 }, { 112, 5 } }, /* link */
    { { 117, 3 }, { 120, 3 } }, /* badge */
    { { 123, 6 }, { 129, 6 } }, /* trash */
    { { 135, 7 }, { 142, 7 } }, /* trash_full */
    { { 149, 4 }, { 153, 4 } }, /* thispc (1.5 stage 14b) */
    { { 157, 3 }, { 160, 3 } }, /* disk */
    { { 163, 5 }, { 168, 11 } }, /* memory */
    { { 179, 3 }, { 182, 3 } }, /* cd */
};

const uint32_t cicon_point_count = sizeof(cicon_points) / sizeof(cicon_points[0]);
const uint32_t cicon_shape_count = sizeof(cicon_shapes) / sizeof(cicon_shapes[0]);
