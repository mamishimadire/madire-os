/* Scancode set 1 decoder, US layout (see scancode.h). */
#include "drivers/keyboard/scancode.h"

#define BREAK_BIT   0x80u
#define PREFIX_E0   0xE0u
#define PREFIX_E1   0xE1u
#define PAUSE_BYTES 5u          /* after E1: 1D 45 E1 9D C5 */

/* Internal modifier bits (left/right tracked separately). */
#define MOD_LSHIFT 0x01u
#define MOD_RSHIFT 0x02u
#define MOD_LCTRL  0x04u
#define MOD_RCTRL  0x08u
#define MOD_LALT   0x10u
#define MOD_RALT   0x20u

struct key_def {
    uint8_t key;
    char normal;
    char shifted;
};

#define CH(n, s) { KEY_CHAR, n, s }

/* Set 1 make codes without prefix. Keypad keys report their digit/symbol
 * (Num Lock is assumed on; Num Lock state is not tracked in 0.4). */
static const struct key_def base_keys[0x80] = {
    [0x01] = { KEY_ESC, 0, 0 },
    [0x02] = CH('1', '!'), [0x03] = CH('2', '@'), [0x04] = CH('3', '#'),
    [0x05] = CH('4', '$'), [0x06] = CH('5', '%'), [0x07] = CH('6', '^'),
    [0x08] = CH('7', '&'), [0x09] = CH('8', '*'), [0x0A] = CH('9', '('),
    [0x0B] = CH('0', ')'), [0x0C] = CH('-', '_'), [0x0D] = CH('=', '+'),
    [0x0E] = { KEY_BACKSPACE, '\b', '\b' },
    [0x0F] = { KEY_TAB, '\t', '\t' },
    [0x10] = CH('q', 'Q'), [0x11] = CH('w', 'W'), [0x12] = CH('e', 'E'),
    [0x13] = CH('r', 'R'), [0x14] = CH('t', 'T'), [0x15] = CH('y', 'Y'),
    [0x16] = CH('u', 'U'), [0x17] = CH('i', 'I'), [0x18] = CH('o', 'O'),
    [0x19] = CH('p', 'P'), [0x1A] = CH('[', '{'), [0x1B] = CH(']', '}'),
    [0x1C] = { KEY_ENTER, '\n', '\n' },
    [0x1D] = { KEY_LCTRL, 0, 0 },
    [0x1E] = CH('a', 'A'), [0x1F] = CH('s', 'S'), [0x20] = CH('d', 'D'),
    [0x21] = CH('f', 'F'), [0x22] = CH('g', 'G'), [0x23] = CH('h', 'H'),
    [0x24] = CH('j', 'J'), [0x25] = CH('k', 'K'), [0x26] = CH('l', 'L'),
    [0x27] = CH(';', ':'), [0x28] = CH('\'', '"'), [0x29] = CH('`', '~'),
    [0x2A] = { KEY_LSHIFT, 0, 0 },
    [0x2B] = CH('\\', '|'),
    [0x2C] = CH('z', 'Z'), [0x2D] = CH('x', 'X'), [0x2E] = CH('c', 'C'),
    [0x2F] = CH('v', 'V'), [0x30] = CH('b', 'B'), [0x31] = CH('n', 'N'),
    [0x32] = CH('m', 'M'), [0x33] = CH(',', '<'), [0x34] = CH('.', '>'),
    [0x35] = CH('/', '?'),
    [0x36] = { KEY_RSHIFT, 0, 0 },
    [0x37] = CH('*', '*'),                      /* keypad * */
    [0x38] = { KEY_LALT, 0, 0 },
    [0x39] = { KEY_SPACE, ' ', ' ' },
    [0x3A] = { KEY_CAPSLOCK, 0, 0 },
    [0x3B] = { KEY_F1, 0, 0 }, [0x3C] = { KEY_F2, 0, 0 }, [0x3D] = { KEY_F3, 0, 0 },
    [0x3E] = { KEY_F4, 0, 0 }, [0x3F] = { KEY_F5, 0, 0 }, [0x40] = { KEY_F6, 0, 0 },
    [0x41] = { KEY_F7, 0, 0 }, [0x42] = { KEY_F8, 0, 0 }, [0x43] = { KEY_F9, 0, 0 },
    [0x44] = { KEY_F10, 0, 0 },
    [0x45] = { KEY_NUMLOCK, 0, 0 },
    [0x46] = { KEY_SCROLLLOCK, 0, 0 },
    [0x47] = CH('7', '7'), [0x48] = CH('8', '8'), [0x49] = CH('9', '9'),
    [0x4A] = CH('-', '-'),
    [0x4B] = CH('4', '4'), [0x4C] = CH('5', '5'), [0x4D] = CH('6', '6'),
    [0x4E] = CH('+', '+'),
    [0x4F] = CH('1', '1'), [0x50] = CH('2', '2'), [0x51] = CH('3', '3'),
    [0x52] = CH('0', '0'), [0x53] = CH('.', '.'),
    [0x57] = { KEY_F11, 0, 0 },
    [0x58] = { KEY_F12, 0, 0 },
};

/* Set 1 codes after an E0 prefix. */
static const struct key_def extended_keys[0x80] = {
    [0x1C] = { KEY_KP_ENTER, '\n', '\n' },
    [0x1D] = { KEY_RCTRL, 0, 0 },
    [0x35] = CH('/', '/'),                      /* keypad / */
    [0x37] = { KEY_PRINTSCREEN, 0, 0 },
    [0x38] = { KEY_RALT, 0, 0 },
    [0x47] = { KEY_HOME, 0, 0 },
    [0x48] = { KEY_UP, 0, 0 },
    [0x49] = { KEY_PGUP, 0, 0 },
    [0x4B] = { KEY_LEFT, 0, 0 },
    [0x4D] = { KEY_RIGHT, 0, 0 },
    [0x4F] = { KEY_END, 0, 0 },
    [0x50] = { KEY_DOWN, 0, 0 },
    [0x51] = { KEY_PGDN, 0, 0 },
    [0x52] = { KEY_INSERT, 0, 0 },
    [0x53] = { KEY_DELETE, 0, 0 },
    [0x5B] = { KEY_LGUI, 0, 0 },
    [0x5C] = { KEY_RGUI, 0, 0 },
    [0x5D] = { KEY_MENU, 0, 0 },
};

void scancode_init(struct scancode_state *s)
{
    *s = (struct scancode_state){ 0 };
}

bool scancode_is_response(uint8_t byte)
{
    return byte == 0xFA || byte == 0xEE || byte == 0xFE || byte == 0x00 || byte == 0xFF;
}

static uint8_t modifier_bit(uint8_t key)
{
    switch (key) {
    case KEY_LSHIFT: return MOD_LSHIFT;
    case KEY_RSHIFT: return MOD_RSHIFT;
    case KEY_LCTRL:  return MOD_LCTRL;
    case KEY_RCTRL:  return MOD_RCTRL;
    case KEY_LALT:   return MOD_LALT;
    case KEY_RALT:   return MOD_RALT;
    default:         return 0;
    }
}

static uint8_t event_flags(const struct scancode_state *s, bool pressed, bool extended)
{
    uint8_t f = 0;
    if (pressed)
        f |= KEY_PRESSED;
    if (s->mods & (MOD_LSHIFT | MOD_RSHIFT))
        f |= KEY_SHIFT;
    if (s->mods & (MOD_LCTRL | MOD_RCTRL))
        f |= KEY_CTRL;
    if (s->mods & (MOD_LALT | MOD_RALT))
        f |= KEY_ALT;
    if (s->caps_on)
        f |= KEY_CAPS;
    if (extended)
        f |= KEY_EXTENDED;
    return f;
}

bool scancode_decode(struct scancode_state *s, uint8_t byte, struct key_event *out)
{
    if (s->pause_left) {
        if (--s->pause_left)
            return false;
        *out = (struct key_event){ KEY_PAUSE, 0, event_flags(s, true, false), byte };
        return true;
    }
    if (byte == PREFIX_E1) {
        s->prefix = 0;
        s->pause_left = PAUSE_BYTES;
        return false;
    }
    if (byte == PREFIX_E0) {
        s->prefix = PREFIX_E0;
        return false;
    }

    bool extended = s->prefix == PREFIX_E0;
    s->prefix = 0;
    bool pressed = !(byte & BREAK_BIT);
    uint8_t code = byte & (uint8_t)~BREAK_BIT;

    if (extended && (code == 0x2A || code == 0x36))
        return false;                               /* fake shift */

    const struct key_def *def = extended ? &extended_keys[code] : &base_keys[code];
    uint8_t key = def->key ? def->key : KEY_UNKNOWN;

    uint8_t bit = modifier_bit(key);
    if (bit) {
        if (pressed)
            s->mods |= bit;
        else
            s->mods &= (uint8_t)~bit;
    }
    if (key == KEY_CAPSLOCK) {
        if (pressed && !s->caps_held)               /* ignore auto-repeat */
            s->caps_on = !s->caps_on;
        s->caps_held = pressed;
    }

    char ascii = def->normal;
    if (key == KEY_CHAR) {
        bool shift = s->mods & (MOD_LSHIFT | MOD_RSHIFT);
        bool letter = def->normal >= 'a' && def->normal <= 'z';
        bool upper = letter ? (shift != s->caps_on) : shift;
        ascii = upper ? def->shifted : def->normal;
    }

    *out = (struct key_event){ key, ascii, event_flags(s, pressed, extended), byte };
    return true;
}

const char *scancode_key_name(uint8_t keycode)
{
    static const char *const names[KEY_COUNT] = {
        [KEY_NONE] = "NONE", [KEY_UNKNOWN] = "UNKNOWN", [KEY_CHAR] = "CHAR",
        [KEY_SPACE] = "SPACE", [KEY_ENTER] = "ENTER", [KEY_BACKSPACE] = "BACKSPACE",
        [KEY_TAB] = "TAB", [KEY_ESC] = "ESC",
        [KEY_LSHIFT] = "LSHIFT", [KEY_RSHIFT] = "RSHIFT",
        [KEY_LCTRL] = "LCTRL", [KEY_RCTRL] = "RCTRL",
        [KEY_LALT] = "LALT", [KEY_RALT] = "RALT",
        [KEY_CAPSLOCK] = "CAPSLOCK", [KEY_NUMLOCK] = "NUMLOCK", [KEY_SCROLLLOCK] = "SCROLLLOCK",
        [KEY_F1] = "F1", [KEY_F2] = "F2", [KEY_F3] = "F3", [KEY_F4] = "F4",
        [KEY_F5] = "F5", [KEY_F6] = "F6", [KEY_F7] = "F7", [KEY_F8] = "F8",
        [KEY_F9] = "F9", [KEY_F10] = "F10", [KEY_F11] = "F11", [KEY_F12] = "F12",
        [KEY_UP] = "UP", [KEY_DOWN] = "DOWN", [KEY_LEFT] = "LEFT", [KEY_RIGHT] = "RIGHT",
        [KEY_HOME] = "HOME", [KEY_END] = "END", [KEY_PGUP] = "PGUP", [KEY_PGDN] = "PGDN",
        [KEY_INSERT] = "INSERT", [KEY_DELETE] = "DELETE", [KEY_KP_ENTER] = "KP_ENTER",
        [KEY_PAUSE] = "PAUSE", [KEY_PRINTSCREEN] = "PRINTSCREEN",
        [KEY_LGUI] = "LGUI", [KEY_RGUI] = "RGUI", [KEY_MENU] = "MENU",
    };
    return keycode < KEY_COUNT && names[keycode] ? names[keycode] : "?";
}
