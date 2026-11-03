// Only force-included by the Snow Leopard guest-tool build. Do not add
// classes/categories to the host app or change later guests' Foundation.
#import <Foundation/Foundation.h>
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
// The genuine Tiger Foundation predates NSINTEGER_DEFINED. JSONKit's own
// period ABI declarations must precede our NSUInteger-bearing declarations.
#import "../vendor/source/JSONKit/JSONKit.h"
#endif
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1070
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
#define NSDataWritingAtomic NSAtomicWrite
@interface VIMPeriodPropertyList : NSObject
+ (NSData *)dataWithPropertyList:(id)value format:(NSPropertyListFormat)format options:(NSUInteger)options error:(NSError **)error;
+ (id)propertyListWithData:(NSData *)data options:(NSUInteger)options format:(NSPropertyListFormat *)format error:(NSError **)error;
@end
#define NSPropertyListSerialization VIMPeriodPropertyList
#endif
#if __has_feature(objc_arc)
#ifndef CFBridgingRelease
#define CFBridgingRelease(value) ((__bridge_transfer id)(value))
#endif
#endif
#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#import <ApplicationServices/ApplicationServices.h>
// Snow Leopard publishes one backing pixel per logical display point. It
// predates the distinct HiDPI pixel-dimension APIs; do not weak-call them.
#define CGDisplayModeGetPixelWidth CGDisplayModeGetWidth
#define CGDisplayModeGetPixelHeight CGDisplayModeGetHeight
#import <DiskArbitration/DiskArbitration.h>
#include <sys/mount.h>
#include <stdlib.h>
#include <string.h>
static inline DADiskRef VIMPeriodDiskFromVolume(CFAllocatorRef allocator,DASessionRef session,CFURLRef URL) {
    char path[PATH_MAX];struct statfs info;
    if(!CFURLGetFileSystemRepresentation(URL,true,(UInt8 *)path,sizeof(path))||statfs(path,&info))return NULL;
    return DADiskCreateFromBSDName(allocator,session,info.f_mntfromname);
}
#define DADiskCreateFromVolumePath VIMPeriodDiskFromVolume
static inline void VIMPeriodRandomBuffer(void *buffer,size_t count) {
    unsigned char *bytes=buffer;
    while(count){uint32_t word=arc4random();size_t take=count<sizeof(word)?count:sizeof(word);
        memcpy(bytes,&word,take);bytes+=take;count-=take;}
}
#define arc4random_buf VIMPeriodRandomBuffer
#ifndef O_CLOEXEC
// Darwin 10 has FD_CLOEXEC, not the later atomic open flag. Preserve close
// on exec on this period path; never pass an invented flag to its kernel.
#define O_CLOEXEC 0x40000000
#ifndef O_DIRECTORY
#define O_DIRECTORY 0x20000000
#define VIM_PERIOD_DIRECTORY_CHECK 1
#include <sys/stat.h>
#endif
static inline int VIMPeriodOpen(const char *path,int flags,...) {
    mode_t mode=0;
    if(flags&O_CREAT){va_list args;va_start(args,flags);mode=(mode_t)va_arg(args,int);va_end(args);}
    int nativeFlags=flags&~O_CLOEXEC;
#ifdef VIM_PERIOD_DIRECTORY_CHECK
    nativeFlags&=~O_DIRECTORY;
#endif
    int fd=open(path,nativeFlags,mode);
#ifdef VIM_PERIOD_DIRECTORY_CHECK
    if(fd>=0&&(flags&O_DIRECTORY)){struct stat st;
        if(fstat(fd,&st)||!S_ISDIR(st.st_mode)){close(fd);errno=ENOTDIR;return -1;}}
#endif
    if(fd>=0&&(flags&O_CLOEXEC)&&fcntl(fd,F_SETFD,FD_CLOEXEC)<0){int saved=errno;close(fd);errno=saved;return -1;}
    return fd;
}
#define open VIMPeriodOpen
#endif
@interface VIMPeriodJSON : NSObject
+ (id)JSONObjectWithData:(NSData *)data options:(NSUInteger)options error:(NSError **)error;
+ (NSData *)dataWithJSONObject:(id)object options:(NSUInteger)options error:(NSError **)error;
@end
@interface VIMPeriodRegularExpression : NSObject { NSPredicate *predicate; }
+ (id)regularExpressionWithPattern:(NSString *)pattern options:(NSUInteger)options error:(NSError **)error;
- (NSUInteger)numberOfMatchesInString:(NSString *)string options:(NSUInteger)options range:(NSRange)range;
@end
#define NSJSONSerialization VIMPeriodJSON
#define NSRegularExpression VIMPeriodRegularExpression
@interface NSFileHandle (VIMPeriodNullDevice)
+ (id)fileHandleWithNullDevice;
@end
@interface NSURL (VIMPeriodDirectoryURL)
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
+ (NSURL *)fileURLWithPath:(NSString *)path isDirectory:(BOOL)directory;
#endif
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
- (NSURL *)URLByAppendingPathComponent:(NSString *)component;
#endif
- (NSURL *)URLByAppendingPathComponent:(NSString *)component isDirectory:(BOOL)directory;
@end
@interface NSFileManager (VIMPeriodDirectoryURL)
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
- (BOOL)createDirectoryAtPath:(NSString *)path withIntermediateDirectories:(BOOL)parents attributes:(NSDictionary *)attributes error:(NSError **)error;
#endif
- (BOOL)createDirectoryAtURL:(NSURL *)URL withIntermediateDirectories:(BOOL)parents attributes:(NSDictionary *)attributes error:(NSError **)error;
@end
// Present in 10J3250 Foundation despite missing public declarations. Leopard
// lacks the selectors and gets an isolated implementation in the period file.
@interface NSData (VIMPeriodNativeBase64)
- (id)initWithBase64Encoding:(NSString *)string;
- (NSString *)base64Encoding;
@end
#endif
