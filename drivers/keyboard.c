// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <console.h>
#include <types.h>
#include <keyboard.h>
#include <io.h>

// I should start using defines more. I just avoid them for no reason. I actually quite like them.
#define DATA_PORT 0x60

char input_buffer[4096];
volatile int index = 0;
volatile bool shiftDown = false;
volatile uint32_t prompt_x = 0;
volatile bool command_ready = false;

// I will admit, these arrays were copied from somewhere. I can't remember where.
// I copied them from an OLD OS project that I did, and in the old project, I copied them from somewhere!

char keymap[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', /* 9 */
    '9', '0', '-', '=', '\b',   /* Backspace */
    '\t',           /* Tab */
    'q', 'w', 'e', 'r', /* 19 */
    't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',       /* Enter key */
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
    '(', ')', '_', '+', '\b', /* Backspace */
    '\t', /* Tab */
    'Q', 'W', 'E', 'R', /* 19 */
    'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', /* Enter key */
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

void read_key() {
    int scancode = inb(DATA_PORT);

    // Checks for shift
    if (scancode == 0x2A || scancode == 0x36) { // Left or Right Shift pressed
        shiftDown = true;
    } else if (scancode == 0xAA || scancode == 0xB6) { // Left or Right Shift released
        shiftDown = false;
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

        if (key == '\n') {
            input_buffer[index] = '\0';
            command_ready = true;
            index = 0;
            return;
        } else if (key == '\b') {
            if (index > 0) {
                index--;
            }
            terminal_putchar(key);
            terminal_flush();
            return;
        } else if (index < 4095) {
            input_buffer[index] = key;
            index++;
        }
    
        terminal_putchar(key);
        terminal_flush();
    }
}