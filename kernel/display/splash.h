#ifndef MADIRE_DISPLAY_SPLASH_H
#define MADIRE_DISPLAY_SPLASH_H

/*
 * The splash (1.2 stage 8; decision 17): on a boot whose console is on the
 * framebuffer, the procedural wallpaper with the logo, "MADIRE OS" and
 * "Booting Madire OS" with three dots, instead of the text boot screen.
 * The pictures are wallpaper_core.h's. The wallpaper is drawn once, into
 * the back buffer, with the rest on top; a dots update redraws only the
 * dots' box (from a copy of it saved before the first dots were drawn).
 *
 * The console (fbcon) is held meanwhile: the text boot screen is still
 * written, to vga_shadow only, and comes back whole when the splash
 * closes. It closes on Esc (the text boot screen), on any other key (the
 * text boot screen, then the console opens as before), on a FAILED status
 * line, or on a panic (fbcon takes the screen back for the crash screen).
 * It is not shown with the boot option `verbose`, after a FAILED status, on
 * a boot with any test= option but test=splash, test=splashfail and
 * test=splashpanic. Its buffers are static (no kernel heap).
 *
 * The dots: none lit on the first frame; one more per boot step (a status
 * line) until the timer runs, then one more every 400 ms (40 ticks),
 * whenever thread 0 calls splash_tick(); after three, none again. The test
 * option `still` freezes them. At System ready (1.2 stage 10) the splash
 * goes to its ready state: the dots stop and "Booting Madire OS" gives way
 * to "Press any key to open the console" (wallpaper_core.h).
 *
 * The serial log of a boot without `still` or a test=splash option is the
 * same as without the splash (nothing is logged). With them, the splash
 * logs its lines ("Splash: ...", "splash test: ...", "display: frame N").
 * Only thread 0 draws (decision 6).
 */

#include <stdbool.h>

/* The boot options, once the command line is known: `verbose`, `still`,
 * whether the boot's test= options allow the splash, and whether it logs. */
void splash_options(bool verbose, bool still, bool allowed, bool logging);

/* A boot step's status line was written: failed, or not. Before the splash
 * shows, a failure means it never will; while it shows, it closes. */
void splash_step(bool failed);

/* Whether the splash is to be shown (asked before fbcon_start). */
bool splash_wanted(void);

/* After fbcon_start(true): draws the first frame. If it cannot (no
 * layout, or larger than its buffers), the console takes the screen at
 * once. */
void splash_start(void);

bool splash_showing(void);

/* Thread 0, at any time: the timed dots. */
void splash_tick(void);

/* Closes the splash (Esc or another key): the text boot screen comes
 * back. Nothing if it is not showing. */
void splash_close_key(bool esc);

/* Just before "System ready." is logged: the ready state if the console is
 * offered, else the splash closes (the text boot screen says why). Nothing
 * if it is not showing. */
void splash_ready(bool console_offered);

/* 1.3 stage 8: at the first pass of the idle loop after System ready, on
 * the default path (desktop.h), the splash hands the screen to the
 * desktop: it stops showing without the text boot screen being drawn
 * (fbcon_unhold), so the screen goes from the splash's ready state to the
 * desktop's first frame in one copy, the M where it was. Logged only with
 * the splash's lines (`still`): "Splash: handed over to the desktop: the M
 * kept, the splash's text gone". False (nothing done) if it is not
 * showing. */
bool splash_handover(void);

/* test=splash (a test option): four screens for tests/qmp_splash.py, the
 * dots with 0, 1, 2 and 3 lit, each after "display: frame N" and held
 * still for 3 s, then the report-only timing lines. */
void splash_test(void);

#endif
