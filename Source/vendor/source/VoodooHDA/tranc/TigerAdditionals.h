/*
 *  TigerAdditionals.h
 *  VoodooHDA
 *
 *  Created by Andy Vandijck on 12/03/10.
 *  Copyright 2010 AnV Software. All rights reserved.
 *
 */

#ifdef TIGER // needed for compile fix...
#include <IOKit/IOLocks.h>
// The genuine10.4 i386 kernel stdarg.h uses a void* stack cursor, not the
// compiler's newer char* builtin type. Copy that measured pointer ABI.
#if !defined(__i386__) || __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ != 1040
#error Tiger audio additions require the native10.4 i386 kernel ABI
#endif
typedef char vim_tiger_audio_va_cursor_is_pointer[(sizeof(va_list)==sizeof(void *))?1:-1];
#ifndef va_copy
#define va_copy(destination, source) ((destination) = (source))
#endif
int vprintf_vhda(const char *fmt, va_list ap);
int snprintf_vhda(char *text, size_t maxlen, const char *fmt, ...);

#define vprintf vprintf_vhda
#define snprintf snprintf_vhda
// Darwin8 does not export these later string helpers. strncpy/strncat are
// NOT substitutes: they have different size/termination/return contracts.
size_t vim_tiger_audio_strlcpy(char *destination, const char *source, size_t capacity);
size_t vim_tiger_audio_strlcat(char *destination, const char *source, size_t capacity);
#define strlcpy vim_tiger_audio_strlcpy
#define strlcat vim_tiger_audio_strlcat

// The real8R4088 exports the native Mach mutex accessors, but not the
// IOLockLock/Unlock convenience symbols still declared in its SDK.
#define IOLockLock(lock) lck_mtx_lock(IOLockGetMachLock(lock))
#define IOLockUnlock(lock) lck_mtx_unlock(IOLockGetMachLock(lock))

enum {
    kIOUCVariableStructureSize = 0xffffffff
};
#endif

