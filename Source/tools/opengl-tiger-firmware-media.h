#pragma once
// Tiger's public Disk Arbitration API can exclude a firmware-only disk from
// automatic mounting and the corresponding unreadable-unit dialog. No disk
// contents, filesystem registrations, other media or system binaries change.
#include <CoreFoundation/CoreFoundation.h>
#include <DiskArbitration/DiskArbitration.h>
#include <IOKit/IOKitLib.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>
#include "opengl-guest-machine.h"

static inline Boolean VIMTigerFirmwarePaddedString(CFTypeRef value,CFStringRef expected) {
    if(!value||CFGetTypeID(value)!=CFStringGetTypeID())return false;
    CFMutableStringRef trimmed=CFStringCreateMutableCopy(kCFAllocatorDefault,0,value);
    if(!trimmed)return false;
    CFStringTrimWhitespace(trimmed);Boolean matches=CFEqual(trimmed,expected);
    CFRelease(trimmed);return matches;
}
static inline Boolean VIMTigerFirmwareMediaMatches(CFDictionaryRef description,CFTypeRef serial,CFTypeRef model) {
    if(!description||CFGetTypeID(description)!=CFDictionaryGetTypeID()||
       CFDictionaryGetValue(description,kDADiskDescriptionMediaWholeKey)!=kCFBooleanTrue||
       !VIMTigerFirmwarePaddedString(CFDictionaryGetValue(description,kDADiskDescriptionMediaContentKey),CFSTR("GUID_partition_scheme"))||
       !VIMTigerFirmwarePaddedString(serial,CFSTR("VIMFirmwareEFI"))||
       !VIMTigerFirmwarePaddedString(model,CFSTR("QEMU HARDDISK")))return false;
    CFTypeRef size=CFDictionaryGetValue(description,kDADiskDescriptionMediaSizeKey);SInt64 bytes=0;
    return size&&CFGetTypeID(size)==CFNumberGetTypeID()&&
        CFNumberGetValue(size,kCFNumberSInt64Type,&bytes)&&bytes>=16*1024*1024&&bytes<=128*1024*1024;
}
#ifndef VIM_TIGER_FIRMWARE_MEDIA_MATCH_ONLY
static void VIMTigerFirmwareMediaPeek(DADiskRef disk,void *context) {
    (void)context;
    CFDictionaryRef description=DADiskCopyDescription(disk);
    io_service_t media=DADiskCopyIOMedia(disk);CFTypeRef serial=NULL,model=NULL;
    if(media){
        serial=IORegistryEntrySearchCFProperty(media,kIOServicePlane,CFSTR("Serial Number"),
            kCFAllocatorDefault,kIORegistryIterateParents|kIORegistryIterateRecursively);
        model=IORegistryEntrySearchCFProperty(media,kIOServicePlane,CFSTR("Model"),
            kCFAllocatorDefault,kIORegistryIterateParents|kIORegistryIterateRecursively);
    }
    if(VIMTigerFirmwareMediaMatches(description,serial,model)){
        DADiskOptions mask=kDADiskOptionMountAutomatic|kDADiskOptionMountAutomaticNoDefer;
        if((DADiskGetOptions(disk)&mask)&&DADiskSetOptions(disk,mask,false)!=kDAReturnSuccess)
            fputs("Tiger firmware disk could not disable automatic mounting.\n",stderr);
    }
    if(model)CFRelease(model);if(serial)CFRelease(serial);
    if(media)IOObjectRelease(media);if(description)CFRelease(description);
}
static void *VIMTigerFirmwareMediaThread(void *unused) {
    (void)unused;DASessionRef session=DASessionCreate(kCFAllocatorDefault);
    if(!session){fputs("Tiger firmware disk observer could not start.\n",stderr);return NULL;}
    const void *key=kDADiskDescriptionMediaWholeKey,*value=kCFBooleanTrue;
    CFDictionaryRef match=CFDictionaryCreate(kCFAllocatorDefault,&key,&value,1,
        &kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    if(!match){CFRelease(session);return NULL;}
    // Peek runs before staging/mount/dialog admission. Appeared also covers
    // firmware media that Disk Arbitration probed before this boot service.
    // The agent starts as root before login; no timer or polling is involved.
    DARegisterDiskPeekCallback(session,match,0,VIMTigerFirmwareMediaPeek,NULL);
    DARegisterDiskAppearedCallback(session,match,VIMTigerFirmwareMediaPeek,NULL);
    CFRelease(match);
    DASessionScheduleWithRunLoop(session,CFRunLoopGetCurrent(),kCFRunLoopDefaultMode);
    CFRunLoopRun();
    DASessionUnscheduleFromRunLoop(session,CFRunLoopGetCurrent(),kCFRunLoopDefaultMode);
    CFRelease(session);return NULL;
}
static inline Boolean VIMTigerStartFirmwareMediaObserver(void) {
    struct utsname kernel;
    if(geteuid()!=0||uname(&kernel)||strncmp(kernel.release,"8.",2)||!VIMOpenGLGuestMachine())return false;
    pthread_attr_t attributes;pthread_t thread;
    if(pthread_attr_init(&attributes))return false;
    int status=pthread_attr_setdetachstate(&attributes,PTHREAD_CREATE_DETACHED);
    if(!status)status=pthread_create(&thread,&attributes,VIMTigerFirmwareMediaThread,NULL);
    pthread_attr_destroy(&attributes);return !status;
}
#endif
