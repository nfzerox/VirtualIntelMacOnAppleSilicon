#import "VIMKextHelperCache.h"
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <zlib.h>

NSData *VIMKextHelperCache(NSURL *root,NSString *architecture,BOOL includeLocalExtensions,NSError **error) {
    NSMutableArray *values=[NSMutableArray new];
    NSFileManager *fm=NSFileManager.defaultManager;
    NSArray *repositories=includeLocalExtensions?@[@"System/Library/Extensions",@"Library/Extensions"]:@[@"System/Library/Extensions"];
    for(NSString *repository in repositories) {
        char *physical=realpath([root URLByAppendingPathComponent:repository].fileSystemRepresentation,NULL);
        if(!physical)continue;
        NSString *base=[fm stringWithFileSystemRepresentation:physical length:strlen(physical)];free(physical);
        NSURL *directory=[NSURL fileURLWithPath:base isDirectory:YES];
        NSDirectoryEnumerator<NSURL *> *entries=[fm enumeratorAtURL:directory includingPropertiesForKeys:nil
            options:NSDirectoryEnumerationSkipsHiddenFiles errorHandler:^BOOL(NSURL *url,NSError *issue){
                (void)url;(void)issue;return YES;
            }];
        for(NSURL *bundle in entries) {
            if(![bundle.pathExtension isEqual:@"kext"])continue;
            NSDictionary *info=[NSDictionary dictionaryWithContentsOfURL:[bundle URLByAppendingPathComponent:@"Contents/Info.plist"]];
            if(!info)info=[NSDictionary dictionaryWithContentsOfURL:[bundle URLByAppendingPathComponent:@"Info.plist"]];
            id value=info[[@"OSBundleHelper_" stringByAppendingString:architecture]]?:info[@"OSBundleHelper"];
            NSString *identifier=info[@"CFBundleIdentifier"],*version=info[@"CFBundleVersion"];
            if(!value||![identifier isKindOfClass:NSString.class]||![version isKindOfClass:NSString.class])continue;
            NSString *relative=[bundle.path substringFromIndex:base.length];
            NSString *guestPath=[[@"/" stringByAppendingString:repository] stringByAppendingString:relative];
            [values addObject:@{@"Data":value,@"CFBundleIdentifier":identifier,@"CFBundleVersion":version,@"OSBundlePath":guestPath}];
        }
    }
    [values sortUsingComparator:^NSComparisonResult(NSDictionary *a,NSDictionary *b){
        return [a[@"OSBundlePath"] compare:b[@"OSBundlePath"]];
    }];
    // Despite its CFXML enum name, IOKitUser-907 writes this cache as a
    // binary plist. See OSKext.c:_OSKextWriteCache and kext_tools-326's
    // readSystemKextPropertyValues. Empty arrays are significant: omitting
    // the file makes kextd queue a catalogue reset during the reboot lock.
    NSData *plist=[NSPropertyListSerialization dataWithPropertyList:values format:NSPropertyListBinaryFormat_v1_0 options:0 error:error];
    if(!plist)return nil;
    z_stream stream={0};
    int status=deflateInit2(&stream,Z_DEFAULT_COMPRESSION,Z_DEFLATED,15+16,8,Z_DEFAULT_STRATEGY);
    if(status!=Z_OK||plist.length>UINT_MAX) {
        if(status==Z_OK)deflateEnd(&stream);
        if(error)*error=[NSError errorWithDomain:NSPOSIXErrorDomain code:EIO userInfo:nil];return nil;
    }
    NSMutableData *gzip=[NSMutableData dataWithLength:deflateBound(&stream,(uLong)plist.length)];
    stream.next_in=(Bytef *)plist.bytes;stream.avail_in=(uInt)plist.length;
    stream.next_out=gzip.mutableBytes;stream.avail_out=(uInt)gzip.length;
    status=deflate(&stream,Z_FINISH);gzip.length=stream.total_out;deflateEnd(&stream);
    if(status!=Z_STREAM_END) {
        if(error)*error=[NSError errorWithDomain:NSPOSIXErrorDomain code:EIO userInfo:nil];return nil;
    }
    return gzip;
}
