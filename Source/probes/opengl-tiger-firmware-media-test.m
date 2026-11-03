// Pure selection boundary. Never contacts Disk Arbitration or changes media.
#import <Foundation/Foundation.h>
#define VIM_TIGER_FIRMWARE_MEDIA_MATCH_ONLY 1
#include "../tools/opengl-tiger-firmware-media.h"
#include <assert.h>
int main(void) {
    CFMutableDictionaryRef description=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(description,kDADiskDescriptionMediaWholeKey,kCFBooleanTrue);
    CFDictionarySetValue(description,kDADiskDescriptionMediaContentKey,CFSTR("GUID_partition_scheme"));
    SInt64 size=68173312;CFNumberRef bytes=CFNumberCreate(NULL,kCFNumberSInt64Type,&size);
    CFDictionarySetValue(description,kDADiskDescriptionMediaSizeKey,bytes);CFRelease(bytes);
    assert(VIMTigerFirmwareMediaMatches(description,CFSTR("VIMFirmwareEFI       "),CFSTR("QEMU HARDDISK                           ")));
    assert(!VIMTigerFirmwareMediaMatches(description,CFSTR("QM00001"),CFSTR("QEMU HARDDISK")));
    assert(!VIMTigerFirmwareMediaMatches(description,CFSTR("VIMFirmwareEFI-other"),CFSTR("QEMU HARDDISK")));
    assert(!VIMTigerFirmwareMediaMatches(description,CFSTR("VIMFirmwareEFI"),CFSTR("QEMU DVD-ROM")));
    assert(!VIMTigerFirmwareMediaMatches(description,kCFBooleanTrue,CFSTR("QEMU HARDDISK")));
    CFDictionarySetValue(description,kDADiskDescriptionMediaWholeKey,kCFBooleanFalse);
    assert(!VIMTigerFirmwareMediaMatches(description,CFSTR("VIMFirmwareEFI"),CFSTR("QEMU HARDDISK")));
    CFDictionarySetValue(description,kDADiskDescriptionMediaWholeKey,kCFBooleanTrue);
    CFDictionarySetValue(description,kDADiskDescriptionMediaContentKey,CFSTR("Apple_HFS"));
    assert(!VIMTigerFirmwareMediaMatches(description,CFSTR("VIMFirmwareEFI"),CFSTR("QEMU HARDDISK")));
    CFDictionarySetValue(description,kDADiskDescriptionMediaContentKey,CFSTR("GUID_partition_scheme"));
    size=64LL*1024*1024*1024;bytes=CFNumberCreate(NULL,kCFNumberSInt64Type,&size);
    CFDictionarySetValue(description,kDADiskDescriptionMediaSizeKey,bytes);CFRelease(bytes);
    assert(!VIMTigerFirmwareMediaMatches(description,CFSTR("VIMFirmwareEFI"),CFSTR("QEMU HARDDISK")));
    CFDictionarySetValue(description,kDADiskDescriptionMediaSizeKey,kCFBooleanTrue);
    assert(!VIMTigerFirmwareMediaMatches(description,CFSTR("VIMFirmwareEFI"),CFSTR("QEMU HARDDISK")));
    assert(!VIMTigerFirmwareMediaMatches(NULL,CFSTR("VIMFirmwareEFI"),CFSTR("QEMU HARDDISK")));
    CFRelease(description);puts("Tiger firmware-only media selection PASS");return 0;
}
