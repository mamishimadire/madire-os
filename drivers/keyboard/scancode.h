#ifndef MADIRE_DRIVERS_KEYBOARD_SCANCODE_H
#define MADIRE_DRIVERS_KEYBOARD_SCANCODE_H

/*
 * PS/2 scancode set 1 decoder, US layout.
 *
 * Set 1 is what the i8042 delivers with translation enabled (the BIOS
 * default). Pure module: no hardware access, printing or panics, so it is
 * unit-tested on the host (tests/unit/test_scancode.c).
 *
 *   byte < 0x80       make (key pressed)
 *   byte | 0x80       break (key released)
 *   E0 xx             extended key (arrows, right Ctrl/Alt, keypad Enter, ...)
 *   E0 2A / E0 AA,
 *   E0 36 / E0 B6     "fake shifts" around Print Screen etc.: ignored
 *   E1 1D 45 E1 9D C5 Pause: one KEY_PAUSE press, no release
 *
 * Caps Lock, Num Lock and Scroll Lock toggle once per press (auto-repeat
 * ignored); Num Lock is on after scancode_init (1.1). The numeric keypad
 * follows Num Lock: on, it types digits and '.'; off, it gives the
 * navigation keys (Home, Up, PgUp, Left, Right, End, Down, PgDn, Insert,
 * Delete) without KEY_EXTENDED, and keypad 5 gives KEY_UNKNOWN. 0x56 is the
 * ISO key (between left Shift and Z): '\' and, with Shift, '|'.
 */

#include <stdbool.h>
#include <stdint.h>

enum keycode {
    KEY_NONE = 0,
    KEY_UNKNOWN,
    KEY_CHAR,           /* printable character: see key_event.ascii */
    KEY_SPACE,
    KEY_ENTER,
    KEY_BACKSPACE,
    KEY_TAB,
    KEY_ESC,
    KEY_LSHIFT, KEY_RSHIFT,
    KEY_LCTRL, KEY_RCTRL,
    KEY_LALT, KEY_RALT,
    KEY_CAPSLOCK, KEY_NUMLOCK, KEY_SCROLLLOCK,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_HOME, KEY_END, KEY_PGUP, KEY_PGDN, KEY_INSERT, KEY_DELETE,
    KEY_KP_ENTER,
    KEY_PAUSE,
    KEY_PRINTSCREEN,
    KEY_LGUI, KEY_RGUI, KEY_MENU,
    KEY_COUNT
};

/* key_event.flags */
#define KEY_PRESSED   0x01u   /* make; clear = break (released) */
#define KEY_SHIFT     0x02u   /* either Shift held */
#define KEY_CTRL      0x04u   /* either Ctrl held */
#define KEY_ALT       0x08u   /* either Alt held */
#define KEY_CAPS      0x10u   /* Caps Lock on */
#define KEY_EXTENDED  0x20u   /* came with an E0 prefix */

struct key_event {
    uint8_t keycode;        /* enum keycode */
    char ascii;             /* character for this key and modifiers, 0 if none */
    uint8_t flags;
    uint8_t scancode;       /* last byte of the sequence (break bit included) */
};

struct scancode_state {
    uint8_t prefix;         /* 0xE0 while an extended code is pending */
    uint8_t pause_left;     /* bytes of the Pause sequence still to swallow */
    uint8_t mods;           /* internal left/right modifier bits */
    bool caps_on;
    bool caps_held;         /* Caps Lock currently down (ignore auto-repeat) */
    bool num_on;            /* Num Lock on (the keypad types digits) */
    bool num_held;
    bool scroll_on;
    bool scroll_held;
};

/* Clears the state; Num Lock starts on. */
void scancode_init(struct scancode_state *s);

/* The lock states as the PS/2 light byte: bit 0 Scroll Lock, bit 1 Num Lock,
 * bit 2 Caps Lock. */
uint8_t scancode_lights(const struct scancode_state *s);

/* Feeds one byte. Returns true and fills *out when the byte completes a key
 * event; false for prefixes, ignored bytes and swallowed Pause bytes. */
bool scancode_decode(struct scancode_state *s, uint8_t byte, struct key_event *out);

/* Bytes the keyboard sends as replies, not keys: ACK 0xFA, echo 0xEE,
 * resend 0xFE, errors 0x00 / 0xFF. None of them is a valid set-1 key code. */
bool scancode_is_response(uint8_t byte);

/* Short upper-case name, e.g. "CHAR", "UP", "LSHIFT". */
const char *scancode_key_name(uint8_t keycode);

#endif
