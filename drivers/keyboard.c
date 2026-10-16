// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <console.h>
#include <types.h>
#include <keyboard.h>
#include <io.h>
#include <tty.h>

// I should start using defines more. I just avoid them for no reason. I actually quite like them.
#define DATA_PORT 0x60

volatile bool shiftDown = false;
volatile bool ctrlDown = false;
volatile bool altDown = false;
volatile uint32_t prompt_x = 0;

// I will admit, these arrays were copied from somewhere. I can't remember where.
// I copied them from an OLD OS project that I did, and in the old project, I copied them from somewhere!

char keymap[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', /* 9 */
    '9', '0', '-', '=', 0x7F,   /* Backspace: DEL, as Linux's console sends */
    '\t',           /* Tab */
    'q', 'w', 'e', 'r', /* 19 */
    't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\r',       /* Enter: CR (ICRNL makes it \n) */
    0,          /* 29   - Control */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',   /* 39 */
    '\'', '`',   0,     /* Left shift */
    '\\', 'z', 'x', 'c', 'v', 'b', 'n',         /* 49 */
    'm', ',', '.', '/',   0,                    /* Right shift */
    '*',
    0,  /* Alt */
    ' ',    /* Space bar */
    0,  /* Caps lock */
    0,  /* 59 - F1 key ... > */
    0,   0,   0,   0,   0,   0,   0,   0,
    0,  /* < ... F10 */
    0,  /* 69 - Num lock*/
    0,  /* Scroll Lock */
    0,  /* Home key */
    0,  /* Up Arrow */
    0,  /* Page Up */
    '-',
    0,  /* Left Arrow */
    0,
    0,  /* Right Arrow */
    '+',
    0,  /* 79 - End key*/
    0,  /* Down Arrow */
    0,  /* Page Down */
    0,  /* Insert Key */
    0,  /* Delete Key */
    0,   0,   0,
    0,  /* F11 Key */
    0,  /* F12 Key */
    0,  /* All other keys are undefined */
};

char keymap_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', /* 9 */
    '(', ')', '_', '+', 0x7F, /* Backspace */
    '\t', /* Tab */
    'Q', 'W', 'E', 'R', /* 19 */
    'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\r', /* Enter key */
    0, /* 29 - Control */
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', /* 39 */
    '"', '~', 0, /* Left shift */
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', /* 49 */
    'M', '<', '>', '?', 0, /* Right shift */
    '*',
    0, /* Alt */
    ' ', /* Space bar */
    0, /* Caps lock */
    0, /* 59 - F1 key ... > */
    0, 0, 0, 0, 0, 0, 0, 0,
    0, /* < ... F10 */
    0, /* 69 - Num lock*/
    0, /* Scroll Lock */
    0, /* Home key */
    0, /* Up Arrow */
    0, /* Page Up */
    '-',
    0, /* Left Arrow */
    0,
    0, /* Right Arrow */
    '+',
    0, /* 79 - End key*/
    0, /* Down Arrow */
    0, /* Page Down */
    0, /* Insert Key */
    0, /* Delete Key */
    0, 0, 0,
    0, /* F11 Key */
    0, /* F12 Key */
    0, /* All other keys are undefined */
};

// I didn't even know you needed this.
void keyboard_init() {
    while (inb(0x64) & 0x02);
    outb(0x64, 0xAE);
    
    while (inb(0x64) & 0x02);
    outb(0x60, 0xF4);
    
    if (inb(0x64) & 0x01) {
        inb(0x60);
    }
}

static int kbd_mode = KBD_XLATE;

void keyboard_set_mode(int mode) {
    kbd_mode = mode;
    shiftDown = ctrlDown = altDown = false;     // whatever was held went by raw
}

int keyboard_mode(void) {
    return kbd_mode;
}

// Linux's keycodes for the 0xE0-prefixed scancodes; the others are the
// scancode itself.
static uint8_t e0_keycode(int code) {
    switch (code) {
        case 0x1C: return 96;   // keypad Enter
        case 0x1D: return 97;   // right Ctrl
        case 0x35: return 98;   // keypad /
        case 0x37: return 99;   // Print Screen
        case 0x38: return 100;  // right Alt
        case 0x47: return 102;  // Home
        case 0x48: return 103;  // Up
        case 0x49: return 104;  // Page Up
        case 0x4B: return 105;  // Left
        case 0x4D: return 106;  // Right
        case 0x4F: return 107;  // End
        case 0x50: return 108;  // Down
        case 0x51: return 109;  // Page Down
        case 0x52: return 110;  // Insert
        case 0x53: return 111;  // Delete
        case 0x5B: return 125;  // left Windows
        case 0x5C: return 126;  // right Windows
        case 0x5D: return 127;  // Menu
        default:   return 0;
    }
}

void read_key() {
    static bool extended = false;   // saw an 0xE0 prefix byte
    if ((inb(0x64) & 0x21) == 0x21) return;     // the mouse's byte: IRQ 12 takes it
    int scancode = inb(DATA_PORT);

    if (kbd_mode == KBD_RAW) {
        tty_input(&tty_console, (char)scancode);
        return;
    }
    if (kbd_mode == KBD_MEDIUMRAW) {
        if (scancode == 0xE0) { extended = true; return; }
        uint8_t release = scancode & 0x80, code = scancode & 0x7F;
        if (extended) {
            extended = false;
            if (code == 0x2A || code == 0x36) return;    // fake shifts
            code = e0_keycode(code);
            if (!code) return;
        }
        tty_input(&tty_console, (char)(code | release));
        return;
    }

    // Extended keys (arrows, right-ctrl, keypad-/, ...) arrive as 0xE0 + code,
    // some with fake-shift bytes (0xE0 0x2A / 0xE0 0xAA) that must not touch
    // shiftDown. The editing keys become the escape sequences Linux's
    // console sends (TERM=linux); the rest are swallowed.
    if (scancode == 0xE0) {
        extended = true;
        return;
    }
    if (extended) {
        extended = false;
        const char* seq = 0;
        switch (scancode) {
            case 0x1D: ctrlDown = true; return;       // right Ctrl
            case 0x9D: ctrlDown = false; return;
            case 0x38: altDown = true; return;        // right Alt
            case 0xB8: altDown = false; return;
            case 0x48: seq = "\x1B[A"; break;         // up
            case 0x50: seq = "\x1B[B"; break;         // down
            case 0x4D: seq = "\x1B[C"; break;         // right
            case 0x4B: seq = "\x1B[D"; break;         // left
            case 0x47: seq = "\x1B[1~"; break;        // Home
            case 0x4F: seq = "\x1B[4~"; break;        // End
            case 0x52: seq = "\x1B[2~"; break;        // Insert
            case 0x53: seq = "\x1B[3~"; break;        // Delete
            case 0x49: seq = "\x1B[5~"; break;        // Page Up
            case 0x51: seq = "\x1B[6~"; break;        // Page Down
            case 0x1C: tty_input(&tty_console, '\r'); return;       // keypad Enter
            case 0x35: tty_input(&tty_console, '/'); return;        // keypad /
            default: return;
        }
        while (*seq) tty_input(&tty_console, *seq++);
        return;
    }

    // Checks for shift and control
    if (scancode == 0x2A || scancode == 0x36) { // Left or Right Shift pressed
        shiftDown = true;
    } else if (scancode == 0xAA || scancode == 0xB6) { // Left or Right Shift released
        shiftDown = false;
    } else if (scancode == 0x1D) {              // Left Ctrl pressed
        ctrlDown = true;
    } else if (scancode == 0x9D) {              // Left Ctrl released
        ctrlDown = false;
    } else if (scancode == 0x38) {              // Left Alt pressed
        altDown = true;
    } else if (scancode == 0xB8) {              // Left Alt released
        altDown = false;
    } else if (scancode < 0x80) {
        char key;
        if (shiftDown){
            key = keymap_shift[scancode];
        } else{
            key = keymap[scancode];
        }

        if (key == 0) {
            return;
        }

        // Ctrl+letter is the ASCII control code: Ctrl+C = 0x03, Ctrl+D = 0x04.
        if (ctrlDown) {
            if (key >= 'a' && key <= 'z') key = (char)(key - 'a' + 1);
            else if (key >= 'A' && key <= 'Z') key = (char)(key - 'A' + 1);
            else if (key == '[') key = 0x1B;            // ^[ = ESC
            else if (key == '\\') key = 0x1C;           // ^\ = SIGQUIT
            else if (key == ']') key = 0x1D;
            else if (key == ' ' || key == '@' || key == '2') key = 0x00;
            else if (key == '/' || key == '_' || key == '-') key = 0x1F;
        }

        // Alt+key is "meta": ESC, then the key -- as terminals send it.
        if (altDown) tty_input(&tty_console, 0x1B);

        // Editing, echo and line handling all belong to the terminal now.
        tty_input(&tty_console, key);
    }
}
