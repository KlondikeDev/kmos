// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <types.h>

void utoa_hex_padded(u32 value, char* buffer, int width) {
    const char* digits = "0123456789ABCDEF";
    int i = 0;

    // Add "0x" prefix
    buffer[i++] = '0';
    buffer[i++] = 'x';

    // Convert to hex with padding
    for (int digit = width - 1; digit >= 0; digit--) {
        buffer[i++] = digits[(value >> (digit * 4)) & 0xF];
    }

    buffer[i] = '\0';
}

void utoa_hex64_padded(u64 value, char* buffer) {
    const char* digits = "0123456789ABCDEF";
    int i = 0;

    // Add "0x" prefix
    buffer[i++] = '0';
    buffer[i++] = 'x';

    // Convert to hex with 16 digits for 64-bit
    for (int digit = 15; digit >= 0; digit--) {
        buffer[i++] = digits[(value >> (digit * 4)) & 0xF];
    }

    buffer[i] = '\0';
}

void utoa(u32 value, char* buffer, int base) {
    const char* digits = "0123456789ABCDEF";
    int i = 0;

    // Safety: basic base check
    if (base < 2 || base > 16) {
        buffer[0] = '\0';
        return;
    }

    // Special handling for hex (base 16) with 0x prefix and padding
    if (base == 16) {

        int started = 0;
        for (int digit = 7; digit >= 0; digit--) {
            int nibble = (value >> (digit * 4)) & 0xF;
            if (nibble != 0 || started || digit == 0) {
                buffer[i++] = digits[nibble];
                started = 1;
            }
        }
    
    
        buffer[i] = '\0';
        return;
    }

    // Handle zero for other bases
    if (value == 0) {
        buffer[i++] = '0';
        buffer[i] = '\0';
        return;
    }

    // Convert number to string (reversed)
    while (value > 0) {
        buffer[i++] = digits[value % base];
        value /= base;
    }

    buffer[i] = '\0';

    // Reverse string
    for (int start = 0, end = i - 1; start < end; start++, end--) {
        char tmp = buffer[start];
        buffer[start] = buffer[end];
        buffer[end] = tmp;
    }
}

void utoa64(u64 value, char* buffer, int base) {
    const char* digits = "0123456789ABCDEF";
    int i = 0;

    if (base < 2 || base > 16) {
        buffer[0] = '\0';
        return;
    }

    if (base == 16) {
        int started = 0;
        for (int digit = 15; digit >= 0; digit--) {
            int nibble = (value >> (digit * 4)) & 0xF;
            if (nibble != 0 || started || digit == 0) {
                buffer[i++] = digits[nibble];
                started = 1;
            }
        }
        buffer[i] = '\0';
        return;
    }

    if (value == 0) {
        buffer[i++] = '0';
        buffer[i] = '\0';
        return;
    }

    while (value > 0) {
        buffer[i++] = digits[value % base];
        value /= base;
    }

    buffer[i] = '\0';

    for (int start = 0, end = i - 1; start < end; start++, end--) {
        char tmp = buffer[start];
        buffer[start] = buffer[end];
        buffer[end] = tmp;
    }
}

void itoa64(i64 value, char* buffer) {
    int i = 0;
    int is_negative = 0;

    if (value == 0) {
        buffer[i++] = '0';
        buffer[i] = '\0';
        return;
    }

    if (value < 0) {
        is_negative = 1;
        value = -value;
    }

    while (value > 0) {
        int digit = value % 10;
        buffer[i++] = '0' + digit;
        value /= 10;
    }

    if (is_negative) {
        buffer[i++] = '-';
    }

    buffer[i] = '\0';

    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = buffer[start];
        buffer[start] = buffer[end];
        buffer[end] = temp;
        start++;
        end--;
    }
}

void itoa(i32 value, char* buffer) {
    int i = 0;
    int is_negative = 0;

    // Handle zero
    if (value == 0) {
        buffer[i++] = '0';
        buffer[i] = '\0';
        return;
    }

    // Handle negative numbers
    if (value < 0) {
        is_negative = 1;
        value = -value;
    }

    // Extract digits in reverse order
    while (value > 0) {
        int digit = value % 10;
        buffer[i++] = '0' + digit;
        value /= 10;
    }

    // Add minus sign if negative
    if (is_negative) {
        buffer[i++] = '-';
    }

    buffer[i] = '\0';

    // Reverse the string
    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = buffer[start];
        buffer[start] = buffer[end];
        buffer[end] = temp;
        start++;
        end--;
    }
}

