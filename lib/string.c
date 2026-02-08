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