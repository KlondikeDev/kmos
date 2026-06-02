// Copyright (c) 2026 Joseph Jones
// Licensed under the Kunix License v1.0 or later
// See LICENSE for details

#include <types.h>

char *strcpy(char *destination, const char *source)
{
  if (destination == NULL) return NULL;
  
  int i = 0;
  while (source[i] != '\0')
  {
    destination[i] = source[i];
    i++;
  }

  destination[i] = '\0';
  

  return destination;
}

size_t strlen(const char *s) {
    size_t length = 0;
    while (s[length] != '\0') {
        length++;
    }
    return length;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    while (n > 0 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0){
      return 0;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}