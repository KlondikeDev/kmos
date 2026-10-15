// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// date -- the current time, UTC, in the classic `date` layout.

#include "kunix.h"

int main(void) {
    static const char* days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char* months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    time_t now = time(NULL);
    struct tm t;
    gmtime_r(&now, &t);
    printf("%s %s %2d %02d:%02d:%02d UTC %d\n", days[t.tm_wday], months[t.tm_mon],
           t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, t.tm_year + 1900);
    return 0;
}
