/*
 *  TigerAdditionals.cpp
 *  VoodooHDA
 *
 *  Created by Andy Vandijck on 12/03/10.
 *  Copyright 2010 AnV Software. All rights reserved.
 *
 *  Slice, 05.10.2010 some additions
 */

#ifdef TIGER // needed for compile fix
#include <sys/types.h>
#include <stdarg.h>
#include <libkern/libkern.h>

int
vprintf_vhda(const char *fmt, va_list ap)
{
	// A native i386 kernel stack cannot hold the upstream64KiB temporary.
	// Bound cold diagnostics without allocating or recursing into the driver.
	char tmp[512];

	vsnprintf(tmp, sizeof(tmp), fmt, ap);
	printf("%s", tmp);

	return 0;
}

int snprintf_vhda(char *text, size_t maxlen, const char *fmt, ...)
{
    va_list ap;
    int retval;
	
    va_start(ap, fmt);
    retval = vsnprintf(text, maxlen, fmt, ap);
    va_end(ap);
	
    return retval;
}

size_t vim_tiger_audio_strlcpy(char *destination, const char *source, size_t capacity)
{
    size_t length = 0;
    while (source[length]) ++length;
    if (capacity) {
        size_t count = length < capacity - 1 ? length : capacity - 1;
        bcopy(source, destination, count);
        destination[count] = 0;
    }
    return length;
}

size_t vim_tiger_audio_strlcat(char *destination, const char *source, size_t capacity)
{
    size_t used = 0, length = 0;
    while (used < capacity && destination[used]) ++used;
    while (source[length]) ++length;
    if (used < capacity) {
        size_t available = capacity - used - 1;
        size_t count = length < available ? length : available;
        bcopy(source, destination + used, count);
        destination[used + count] = 0;
    }
    return used + length;
}

#endif
