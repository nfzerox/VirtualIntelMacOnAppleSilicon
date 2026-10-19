#include "License.h"

#ifndef _COMMON_H
#define _COMMON_H

#undef panic
void panic(const char *str, ...) __attribute__((__noreturn__));

#define ASSERT(expr) do { if (!(expr)) panic("%s: failed assertion '%s'", __FUNCTION__, #expr); } while (0)
#define BUG(msg) panic("%s: %s\n", __FUNCTION__, msg)

#define RELEASE(x) do { if (x) { (x)->release(); (x) = NULL; } } while (0)
#define DELETE(x) do { if (x) { delete (x); (x) = NULL; } } while (0)
#define FREE(x) do { if (x) { freeMem(x); (x) = NULL; } } while (0)

#endif
