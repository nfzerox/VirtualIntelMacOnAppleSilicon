// Stands in for every libc header test-i386.c includes, for the freestanding 32-bit payload.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "flatlibc.h"
#define PRIx64 "llx"
double fabs(double), rint(double), acos(double), asin(double), atan(double), atan2(double, double), cos(double), exp(double), fmod(double, double), log(double), sin(double), sqrt(double), tan(double);
void abort(void);
