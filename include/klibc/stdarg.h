#pragma once

typedef char* va_list;
#define va_start(ap, arg) (ap = (va_list)&arg)
#define va_arg(ap, type) (*(type*)(ap += sizeof(type)))
#define va_end(ap) (ap = (va_list)0)