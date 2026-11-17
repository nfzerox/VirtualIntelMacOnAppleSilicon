#import "VIMPeriodKextcache.h"
#include <mach-o/loader.h>
#include <mach-o/fat.h>
#include <libkern/OSByteOrder.h>
#include <errno.h>
#include <stdlib.h>

NSString *VIMOfflineKextcacheVolumePath(NSURL *root,NSError **error) {
    char *physical=realpath(root.fileSystemRepresentation,NULL);
    if(!physical) {
        if(error)*error=[NSError errorWithDomain:NSPOSIXErrorDomain code:errno userInfo:
            @{NSLocalizedDescriptionKey:@"Could not resolve the installed system’s volume."}];
        return nil;
    }
    NSString *path=[NSFileManager.defaultManager stringWithFileSystemRepresentation:physical length:strlen(physical)];
    free(physical);return path;
}

static BOOL RedirectSlice(uint8_t *bytes,NSUInteger length,BOOL *changed) {
    if(length<sizeof(uint32_t))return NO;
    struct mach_header_64 header;
    if(length<sizeof(header))return NO;
    memcpy(&header,bytes,sizeof(header));
    // Other slices are not executed by our explicit arch -x86_64 invocation.
    if(header.magic!=MH_MAGIC_64||header.cputype!=CPU_TYPE_X86_64)return YES;
    if(header.sizeofcmds>length-sizeof(header))return NO;
    NSUInteger cursor=sizeof(header),end=cursor+header.sizeofcmds;
    for(uint32_t i=0;i<header.ncmds;i++) {
        struct load_command load;
        if(end-cursor<sizeof(load))return NO;
        memcpy(&load,bytes+cursor,sizeof(load));
        if(load.cmdsize<sizeof(load)||load.cmdsize>end-cursor)return NO;
        if(load.cmd==LC_LOAD_WEAK_DYLIB) {
            struct dylib_command dylib;
            if(load.cmdsize<sizeof(dylib))return NO;
            memcpy(&dylib,bytes+cursor,sizeof(dylib));
            if(dylib.dylib.name.offset<sizeof(dylib)||dylib.dylib.name.offset>=load.cmdsize)return NO;
            char *name=(char *)bytes+cursor+dylib.dylib.name.offset;
            NSUInteger room=load.cmdsize-dylib.dylib.name.offset;
            if(!memchr(name,0,room))return NO;
            if(!strcmp(name,"/usr/lib/libdz.dylib")) {
                const char replacement[]="@loader_path/dz.dylib";
                if(room<sizeof(replacement))return NO;
                memset(name,0,room);memcpy(name,replacement,sizeof(replacement));*changed=YES;
            }
        }
        cursor+=load.cmdsize;
    }
    return cursor==end;
}

BOOL VIMPrepareOfflineKextcache(NSURL *copy,BOOL *redirected,NSError **error) {
    if(redirected)*redirected=NO;
    NSMutableData *data=[NSMutableData dataWithContentsOfURL:copy options:0 error:error];
    if(!data)return NO;
    uint8_t *bytes=data.mutableBytes;NSUInteger size=data.length;
    BOOL valid=NO,changed=NO;
    if(size>=sizeof(struct fat_header)) {
        struct fat_header fat;memcpy(&fat,bytes,sizeof(fat));
        if(fat.magic==FAT_CIGAM||fat.magic==FAT_MAGIC) {
            BOOL swap=fat.magic==FAT_CIGAM;
            uint32_t count=swap?OSSwapInt32(fat.nfat_arch):fat.nfat_arch;
            valid=count>0&&count<=(size-sizeof(fat))/sizeof(struct fat_arch);
            for(uint32_t i=0;valid&&i<count;i++) {
                struct fat_arch arch;memcpy(&arch,bytes+sizeof(fat)+i*sizeof(arch),sizeof(arch));
                uint32_t offset=swap?OSSwapInt32(arch.offset):arch.offset;
                uint32_t length=swap?OSSwapInt32(arch.size):arch.size;
                valid=offset<=size&&length<=size-offset&&RedirectSlice(bytes+offset,length,&changed);
            }
        } else valid=RedirectSlice(bytes,size,&changed);
    }
    if(!valid) {
        if(error)*error=[NSError errorWithDomain:NSCocoaErrorDomain code:NSFileReadCorruptFileError
            userInfo:@{NSLocalizedDescriptionKey:@"The private startup-cache tool has an invalid Mach-O dependency table."}];
        return NO;
    }
    if(changed&&![data writeToURL:copy options:0 error:error])return NO;
    if(redirected)*redirected=changed;
    return YES;
}
