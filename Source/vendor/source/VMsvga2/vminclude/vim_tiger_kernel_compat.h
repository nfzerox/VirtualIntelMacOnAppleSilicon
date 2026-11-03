/* Native Darwin8 build support; no replacement kernel exports or ABI shims. */
#ifndef VIM_TIGER_KERNEL_COMPAT_H
#define VIM_TIGER_KERNEL_COMPAT_H
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ != 1040
#error This header is only for the genuine Tiger kernel SDK
#endif
#include <libkern/libkern.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOLocks.h>
#include <pexpert/pexpert.h>
#include <pexpert/i386/boot.h>
/* PE_parse_boot_argn exists in the kernel but is absent from its exported
 * symbol sets. Retain the native parser through the exported older API.
 * All owned callers request UInt32 options. Its unsized string fallback
 * must NEVER receive their four-byte stack destination: the native i386
 * boot_args.CommandLine is BOOT_LINE_LENGTH bytes. Parse into a scratch
 * buffer large enough for that entire line plus NUL, then copy4 bytes. */
static inline boolean_t vim_tiger_parse_boot_option(const char *name,void *output,int size)
{
    if(!name || !output || size!=sizeof(UInt32))return FALSE;
    union {UInt32 value;char text[BOOT_LINE_LENGTH+1];} parsed;
    bzero(&parsed,sizeof(parsed));
    if(!PE_parse_boot_arg(name,&parsed))return FALSE;
    bcopy(&parsed.value,output,sizeof(parsed.value));return TRUE;
}
#define PE_parse_boot_argn vim_tiger_parse_boot_option
/* The10.4u header declares IOLockLock/Unlock, but the real8R4088 kernel
 * exports only the underlying lck_mtx operations and IOLockGetMachLock.
 * xnu-792.22.5 implements the private inlines with those same mutex calls.
 * Use the public accessor, never a guessed IOLock layout or newer export. */
#define IOLockLock(lock) lck_mtx_lock(IOLockGetMachLock(lock))
#define IOLockUnlock(lock) lck_mtx_unlock(IOLockGetMachLock(lock))
#ifndef UINT32_MAX
#define UINT32_MAX 0xffffffffU
#endif
#ifndef UINT64_MAX
#define UINT64_MAX 0xffffffffffffffffULL
#endif

/* Darwin8's libkern has no strlcpy declaration. Keep these three bounded
 * local copies self-contained instead of importing a newer kernel KPI. */
static inline size_t vim_tiger_strlcpy(char *destination,const char *source,size_t capacity)
{
    size_t length=0;
    while(source[length])++length;
    if(capacity){
        size_t count=length<capacity-1?length:capacity-1;
        bcopy(source,destination,count);destination[count]=0;
    }
    return length;
}
#define strlcpy vim_tiger_strlcpy
/* The10.4 kernel's task/address/options/offset/length map overload has the
 * same ownership and range contract as the later createMappingInTask name.
 * This substitution is compile-time only and invokes the native vtable slot. */
#define createMappingInTask map
/* Tiger is a32-bit kernel. Its native map accessor returns IOVirtualAddress;
 * it has no later64-bit getAddress overload. No address truncation is added. */
#define getAddress getVirtualAddress
#define getSize getLength
/* xnu-792.22.5 IOUserClient.cpp explicitly tests0xffffffff for variable
 * input/output counts, but its public header does not name that sentinel. */
#define kIOUCVariableStructureSize 0xffffffffU
#endif
