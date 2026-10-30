#ifndef MADIRE_DISPLAY_DESKTOP_H
#define MADIRE_DISPLAY_DESKTOP_H

/*
 * The desktop, the kernel side (1.3 stage 3; docs/plans/1.3.md, C1, C4, C6;
 * the pictures are desktop_core.h's, the composition compositor.h's). In
 * stage 3 only on a boot with the test option test=desktop (test ISOs; the
 * real ISO and every other boot are as in 1.2): at System ready, if the
 * console is offered and is on the framebuffer, the desktop takes the
 * screen from the console (since stage 4b: the console moves into the
 * Console window, fbcon_window) and the compositor owns the back
 * buffer: the background (the wallpaper with the M logo alone, at the
 * splash's place: the owner's decision in stage 3b), the taskbar with the M button
 * (drawn, its normal state only) and the clock, and the pointer from the
 * first mouse packet on (the 1.2 save-under overlay on the framebuffer).
 *
 * In stage 3 any key handed the screen back to 1.2's full-screen console;
 * stage 4b's Console window replaces this (below). A panic draws 1.2's
 * full-screen crash screen (fbcon, docs/plans/1.3.md, C3).
 *
 * The background cache: slot 506's third area (VM_DESKTOP_BASE), planned
 * and mapped once when the desktop starts, never freed; when it does not
 * fit the share (desktop_core.h), or with the test option desktopnocache,
 * the background is drawn again for each damaged rectangle. The clock is
 * read from the CMOS clock once per second of uptime and redrawn when its
 * text changes.
 *
 * Logged (serial and dmesg): the "Desktop: ..." lines when it starts, and
 * when a key closes it. On a test boot (every boot with test=desktop in
 * stage 3) each composed frame is followed by the marker "display: frame
 * N" and "desktop: clock HH:MM" (the clock it shows). With the test option
 * desktoplog, also "desktop: composed ..." per frame (serial only).
 *
 * 1.3 stage 4a: windows (ui_window.h): the window manager (z-order, focus,
 * the drag by the title bar, the close button, Esc), composed between the
 * background and the taskbar. Each window's surface lies in slot 506's
 * third area after the background cache, in the fixed order Console,
 * About, planned when the desktop starts and mapped only if it still fits
 * the share (desktop_core.h); the Console window's surface is planned and
 * mapped already, though only stage 4b draws that window. A window with no
 * surface (or with the test option desktopnosurface) is painted into the
 * back buffer during composition, clipped to the damaged rectangle
 * (docs/plans/1.3.md, C4, A1). The About window (ui_about.h) opens only
 * with the test option desktopabout, at the start (stage 5's menus will
 * open it). With the About window focused, Esc closes it and every other
 * key does nothing. With desktoplog, also "desktop: ..." lines
 * for each window opened (with its rectangle), raised, focused (or none),
 * dragged (its rectangle when the button is released), closed and each
 * close button state, and the About window's values in full.
 *
 * 1.3 stage 4b: the Console window (docs/plans/1.3.md, C2; decision 5;
 * ui_console.h): the console's 80 x 25 grid at the screen's scale, titled
 * "Console". With no window focused any key opens it (or shows it again)
 * and focuses it, and is not typed (decision 7, as amended in 3b); the
 * first such key starts the console, as at System ready in 1.2 ("Desktop:
 * a key opened the Console window (the key is not typed)" each time). With
 * it focused every key goes to the console unchanged, Esc included. Its
 * close button hides it: the shell session goes on, its surface keeps the
 * grid, and opening it again shows the same content and cursor (the window
 * placed as any window that opens). Without a surface (desktopnosurface,
 * or no room in the share) the compositor paints it from vga_shadow.
 *
 * 1.3 stage 4b, the frame log: a frame's marker and clock line are logged
 * only once settled, when no mouse byte waits after the composition (the
 * driver's counts: every byte queued decoded into a packet or counted
 * bad); frames composed meanwhile are counted, so N may jump, and the
 * marker of the last one comes with the first settled pass of the idle
 * loop. With desktoplog, when more than one frame was composed since the
 * last marker, one report-only line before it: "desktop: burst: <n>
 * frames composed (frames <a> to <b>), the longest composition <us> us
 * (...)".
 *
 * 1.3 stage 5: the menus and the Restart dialog (ui_menus.h, ui_menu.h,
 * ui_dialog.h; docs/plans/1.3.md, C5, decisions 9 and 20): the M button
 * (hover, pressed, open) and the M menu (About, Console, Restart), the
 * desktop menu on a right click (Open Console, About Madire OS), the
 * Restart dialog (Cancel, Restart), and the Windows key, which toggles the
 * M menu from anywhere and never reaches a window. They are painted above
 * everything else, into the back buffer during composition. A row that
 * opens the About window logs its values ("desktop: about ..."); one that
 * opens the Console window logs "Desktop: a menu opened the Console
 * window", and the console starts if it is not running. Restart in the
 * dialog logs "Desktop: Restart chosen in the Restart dialog: restarting"
 * and takes the existing reset path (kernel/arch/x86/reset.c, as the
 * shell's reboot). With desktoplog, also a line for each change: "desktop:
 * menu <M|desktop> opened|closed|...", "desktop: M button: <state>",
 * "desktop: dialog Restart ...". desktopabout stays a test option.
 *
 * 1.3 stage 8, the switch (docs/plans/1.3.md, decision 6; the stage 8
 * decisions D1 and D2): the desktop also shows on the default path, a boot
 * with no test= option whose splash is still showing at the first pass of
 * the idle loop after System ready (desktop_start_default). A key queued
 * by then decides first: Esc keeps 1.2's path (the splash closes and the
 * text boot screen shows, nothing opens); any other key starts the desktop
 * and then opens the Console window (not typed). `verbose`, a FAILED status
 * line, a console not offered and every boot without the splash keep 1.2's
 * full-screen console, with no "Desktop:" line (the refusals are logged on
 * test=desktop boots only). The splash hands the screen over (splash.h):
 * the M stays still and only the splash's text disappears. On the default
 * path the "Desktop: shown ..." line ends "(the default path, 1.3 stage
 * 8)", the pointer is turned on ("Desktop: the pointer shown ...",
 * pointer.h), and the frame markers and clock lines come only with
 * desktoplog; with desktoplog the splash's ready state is also held 3 s
 * before the cut, after its own marker ("display: frame N", then "desktop:
 * hold N over: ...").
 */

#include <stdbool.h>
#include <stdint.h>

/* The boot options: test=desktop (`wanted`), no test= option at all
 * (`default_path`, 1.3 stage 8), desktoplog, desktopnocache;
 * 1.3 stage 4a: desktopnosurface, desktopabout; 1.3 stage 4b: desktoppanic
 * (test-only: once a key has opened the Console window and its frame is
 * logged, "Desktop fault test: ..." and a deliberate panic through the
 * normal panic path; the crash screen is 1.2's full-screen one, C3). */
void desktop_options(bool wanted, bool default_path, bool log, bool nocache, bool nosurface, bool about,
                     bool panic_test);

/* 1.3 stage 4a: the About window's values the desktop cannot read itself:
 * the boot loader's name (Multiboot's, or none) and the usable memory in
 * MB (the boot's "Memory: ... MB usable" figure). */
void desktop_about_info(const char *loader, uint32_t usable_mb);

/* At System ready, after its line: starts the desktop if wanted (above).
 * True if it shows. */
bool desktop_start(bool console_offered);
/* 1.3 stage 8: the default path (`default_path` above: no test= option),
 * at the first pass of the idle loop, unless the first key press queued
 * by then is Esc: starts the desktop if the console is offered and the
 * splash is still showing, taking the screen from it (above). False,
 * silently, otherwise. */
bool desktop_start_default(bool console_offered);

bool desktop_showing(void);

/* Thread 0's idle loop, after pointer_poll(): the clock, then one
 * composition of whatever is damaged. */
void desktop_poll(void);

/* 1.3 stage 4a: each mouse packet, after its move: the pointer's (clamped)
 * hot spot, the left button pressed or released in it; 1.3 stage 5: the
 * right button pressed in it (the menus first, ui_menus.h). */
void desktop_pointer(int32_t x, int32_t y, bool left_down, bool left_up, bool right_down);

/* 1.3 stage 5: true once after a menu row chosen with the pointer opened
 * the Console window (the idle loop then starts the console if it is not
 * running). */
bool desktop_take_console(void);

/* 1.3 stage 4b (replacing stage 4a's desktop_key and stage 3's
 * desktop_close_key): every key event while the desktop shows, pressed or
 * released (1.3 stage 5: its key code, read by the menus and the dialog
 * first, ui_menus.h; then ui_wm_key): DESKTOP_KEY_OPENED: the Console window
 * opened or shown (start the console if it is not running; the key is not
 * typed); DESKTOP_KEY_CONSOLE: the console takes the key; DESKTOP_KEY_TAKEN:
 * nothing more to do. */
enum desktop_key_route { DESKTOP_KEY_TAKEN, DESKTOP_KEY_OPENED, DESKTOP_KEY_CONSOLE };
enum desktop_key_route desktop_key(uint8_t keycode, bool pressed);

#endif
