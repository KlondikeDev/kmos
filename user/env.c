// This file has been generated in whole or in part by a Large Language Model

// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

// env -- print the environment.

#include "kunix.h"

int main(void) {
    for (char** e = environ; e && *e; e++) puts(*e);
    return 0;
}
